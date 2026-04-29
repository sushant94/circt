//
//===- HWToBTOR2PP.cpp - HW to BTOR2++ translation --------------*- C++ -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
//
// Converts a hw module to a BTOR2++ format and prints it out
//
//===----------------------------------------------------------------------===//

#include "circt/Conversion/HWToBTOR2PP.h"
#include "circt/Dialect/Comb/CombDialect.h"
#include "circt/Dialect/Comb/CombOps.h"
#include "circt/Dialect/Comb/CombVisitors.h"
#include "circt/Dialect/FIRRTL/FIRRTLInstanceGraph.h"
#include "circt/Dialect/HW/HWInstanceGraph.h"
#include "circt/Dialect/HW/HWAttributes.h"
#include "circt/Dialect/HW/HWModuleGraph.h"
#include "circt/Dialect/HW/HWOps.h"
#include "circt/Dialect/HW/HWPasses.h"
#include "circt/Dialect/HW/HWTypes.h"
#include "circt/Dialect/HW/HWVisitors.h"
#include "circt/Dialect/HW/PortImplementation.h"
#include "circt/Dialect/SV/SVAttributes.h"
#include "circt/Dialect/SV/SVDialect.h"
#include "circt/Dialect/SV/SVOps.h"
#include "circt/Dialect/SV/SVTypes.h"
#include "circt/Dialect/SV/SVVisitors.h"
#include "circt/Dialect/Seq/SeqDialect.h"
#include "circt/Dialect/Seq/SeqOps.h"
#include "circt/Dialect/Verif/VerifDialect.h"
#include "circt/Dialect/Verif/VerifOps.h"
#include "circt/Dialect/Verif/VerifVisitors.h"
#include "circt/Support/InstanceGraph.h"
#include "circt/Support/InstanceGraphInterface.h"
#include "circt/Support/LLVM.h"
#include "mlir/IR/Value.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include <string_view>

namespace circt {
#define GEN_PASS_DEF_CONVERTHWTOBTOR2PP
#include "circt/Conversion/Passes.h.inc"
} // namespace circt

using namespace circt;
using namespace hw;

namespace {
// The goal here is to traverse the operations in order and convert them one by
// one into btor2
struct ConvertHWToBTOR2PPPass
    : public circt::impl::ConvertHWToBTOR2PPBase<ConvertHWToBTOR2PPPass>,
      public comb::CombinationalVisitor<ConvertHWToBTOR2PPPass>,
      public sv::Visitor<ConvertHWToBTOR2PPPass>,
      public hw::TypeOpVisitor<ConvertHWToBTOR2PPPass>,
      public verif::Visitor<ConvertHWToBTOR2PPPass> {
public:
  using verif::Visitor<ConvertHWToBTOR2PPPass>::visitVerif;

  ConvertHWToBTOR2PPPass(raw_ostream &os) : os(os) {}
  // Executes the pass
  void runOnOperation() override;

private:
  // Output stream in which the btor2 will be emitted
  raw_ostream &os;

  // Create a counter that attributes a unique id to each generated btor2 line
  size_t lid = 1; // btor2 line identifiers usually start at 1
  size_t nclocks = 0;

  // Create maps to keep track of lid associations
  // We need these in order to reference results as operands in btor2

  // Keeps track of the ids associated to each declared sort
  // This is used in order to guarantee that sorts are unique and to allow for
  // instructions to reference the given sorts (key: width, value: LID)
  DenseMap<size_t, size_t> sortToLIDMap;
  // Keeps track of {constant, width} -> LID mappings
  // This is used in order to avoid duplicating constant declarations
  // in the output btor2. It is also useful when tracking
  // constants declarations that aren't tied to MLIR ops.
  DenseMap<APInt, size_t> constToLIDMap;
  // Keeps track of the most recent update line for each operation
  // This allows for operations to be used throughout the btor file
  // with their most recent expression. Btor uses unique identifiers for each
  // instruction, so we need to have an association between those and MLIR Ops.
  DenseMap<Operation *, size_t> opLIDMap;
  // Stores the LID of the associated input.
  // This holds a similar function as the opLIDMap but keeps
  // track of block argument index -> LID mappings
  DenseMap<size_t, size_t> inputLIDs;
  // Stores all of the register declaration ops.
  // This allows for the emission of transition arcs for the regs
  // to be deferred to the end of the pass.
  // This is necessary, as we need to wait for the `next` operation to
  // have been converted to btor2 before we can emit the transition.
  SmallVector<Operation *> regOps;

  // Used to perform a DFS search through the module to declare all operands
  // before they are used
  llvm::SmallMapVector<Operation *, OperandRange::iterator, 16> worklist;

  // Store outputs to be emitted at the end of the module
  SmallVector<StringRef> outputs;

  // Keeps track of operations that have been declared
  DenseSet<Operation *> handledOps;

  // Keeps track of all modules and their dependencies
  DenseMap<StringRef, Operation*> moduleMap;
  DenseMap<Operation*, SmallVector<hw::InstanceOp>> moduleDeps;

  // For each module, keep track of all input/output ports
  DenseMap<Operation*, ModulePortInfo> modulePorts;

  // For each module, also keep track of the correposnding ModulePortLookupInfo
  DenseMap<Operation*, ModulePortLookupInfo> modulePortLookupInfo;

  // For each module, keep track of the LID for each input/output port. ssize_t is the port id, size_t is the LID
  DenseMap<Operation*, DenseMap<ssize_t, size_t>> modulePortLIDs;

  // instanceLIDs
  DenseMap<Operation*, size_t> instanceLIDs;

  // value LIDs
  DenseMap<Value, size_t> valueLIDMap;

  Operation* currentModule;

  const char *indent = "  ";

  // Constants used during the conversion
  static constexpr size_t noLID = -1UL;
  [[maybe_unused]] static constexpr int64_t noWidth = -1L;

  /// Field helper functions
public:
  // Checks if an operation was declared
  // If so, its lid will be returned
  // Otherwise a new lid will be assigned to the op
  size_t getOpLID(Operation *op) {
    // Look for the original operation declaration
    // Make sure that wires are considered when looking for an lid
    Operation *defOp = op;
    auto &f = opLIDMap[defOp];

    // If the op isn't associated to an lid, assign it a new one
    if (!f)
      f = lid++;
    return f;
  }

  // Associates the current lid to an operation
  // The LID is then incremented to maintain uniqueness
  size_t setOpLID(Operation *op) {
    size_t oplid = lid++;
    opLIDMap[op] = oplid;
    return oplid;
  }

  // Associates the current lid to an SSA value.
  // This is needed for values without a defining op, like block arguments.
  size_t setValueLID(Value value) {
    size_t valueLID = lid++;
    valueLIDMap[value] = valueLID;
    return valueLID;
  }

  // Checks if an operation was declared
  // If so, its lid will be returned
  // Otherwise -1 will be returned
  size_t getOpLID(Value value) {
    // if this value is in the valueLIDmap (for instance results)
    if (auto it = valueLIDMap.find(value); it != valueLIDMap.end()) {
      return it->second;
    }

    Operation *defOp = value.getDefiningOp();

    if (auto it = opLIDMap.find(defOp); it != opLIDMap.end())
      return it->second;

    // Check for special case where op is actually a port
    // To do so, we start by checking if our operation is a block argument
    if (BlockArgument barg = dyn_cast<BlockArgument>(value)) {
      // Extract the block argument index and use that to get the line number
      size_t argIdx = barg.getArgNumber();

      if (currentModule) {
        const auto &ports = modulePorts.at(currentModule);
        for (const auto &port : ports) {
          if (port.argNum == argIdx && port.isInput()) {
            ssize_t portId = port.getId();
            auto moduleIt = modulePortLIDs.find(currentModule);
            if (moduleIt != modulePortLIDs.end()) {
              const auto &portLIDs = moduleIt->second;
              auto lidIt = portLIDs.find(portId);
              if (lidIt != portLIDs.end()) {
                return lidIt->second;
              }
            }
            break;
          }
        }
      } else if (auto it = inputLIDs.find(argIdx); it != inputLIDs.end()) {
        // os << "inputLIDs: " << it->second << "\n";
        return it->second;
      }
    } 

    // Return -1 if no LID was found
    os << indent << " noLID\n";
    return noLID;
  }

private:
  // Find a port by its id
  const PortInfo* findPortById(const ModulePortInfo& ports, ssize_t portId) {
    for (const auto& port : ports) {
      if (port.getId() == portId)
        return &port;
    }
    return nullptr;
  }

  StringRef getOpName(Operation *op) {
    if (auto nameAttr = op->getAttrOfType<StringAttr>("sv.namehint"))
      return nameAttr.getValue();
    return "";
  }

  void emitOptionalName(StringRef name) {
    if (!name.empty())
      os << " " << name;
  }

  StringRef getValueName(Value value) {
    if (auto *defOp = value.getDefiningOp())
      return getOpName(defOp);

    if (auto barg = dyn_cast<BlockArgument>(value)) {
      if (currentModule) {
        const auto &ports = modulePorts.at(currentModule);
        for (const auto &port : ports) {
          if (port.argNum == barg.getArgNumber())
            return port.getName();
        }
      }
    }

    return "";
  }

  // Checks if a sort was declared with the given width
  // If so, its lid will be returned
  // Otherwise -1 will be returned
  size_t getSortLID(size_t w) {
    if (auto it = sortToLIDMap.find(w); it != sortToLIDMap.end())
      return it->second;

    // If no lid was found return -1
    return noLID;
  }

  // Associate the sort with a new lid
  size_t setSortLID(size_t w) {
    size_t sortlid = lid;
    // Add the width to the declared sorts along with the associated line id
    sortToLIDMap[w] = lid++;
    return sortlid;
  }

  // Checks if a constant of a given size has been declared.
  // If so, its lid will be returned.
  // Otherwise -1 will be returned.
  size_t getConstLID(int64_t val, size_t w) {
    if (auto it = constToLIDMap.find(APInt(w, val)); it != constToLIDMap.end())
      return it->second;

    // if no lid was found return -1
    return noLID;
  }

  // Associates a constant declaration to a new lid
  size_t setConstLID(int64_t val, size_t w) {
    size_t constlid = lid;
    // Keep track of this value in a constant declaration tracker
    constToLIDMap[APInt(w, val)] = lid++;
    return constlid;
  }

  /// String generation helper functions

  // Generates a sort declaration instruction given a type ("bitvec" or array)
  // and a width.
  void genSort(StringRef type, size_t width) {
    // Check that the sort wasn't already declared
    if (getSortLID(width) != noLID) {
      return; // If it has already been declared then return an empty string
    }

    size_t sortlid = setSortLID(width);

    // Build and return a sort declaration
    os << indent << sortlid << " "
       << "sort"
       << " " << type << " " << width << "\n";
  }

  // Generates an input declaration given a sort lid and a name.
  void genInput(size_t inlid, size_t width, StringRef name) {
    // Retrieve the lid associated with the sort (sid)
    size_t sid = sortToLIDMap.at(width);

    // Generate input declaration
    os << indent << inlid << " "
       << "input"
       << " " << sid << " " << name << "\n";
  }

  // Generates a constant declaration given a value, a width and a name.
  void genConst(APInt value, size_t width, Operation *op) {
    // For now we're going to assume that the name isn't taken, given that hw
    // is already in SSA form
    size_t opLID = getOpLID(op);

    // Retrieve the lid associated with the sort (sid)
    size_t sid = sortToLIDMap.at(width);
    SmallString<1024> bitString;
    // size_t required_width = width / 4;
    // if (required_width > 32) {
    //   bitString.resize(required_width);
    // }

    value.toString(bitString, 16, /*isSigned=*/false);
    os << indent << opLID << " "
       << "consth"
       << " " << sid << " " << bitString.str() << "\n";
  }

  // Generates a zero constant expression
  size_t genZero(size_t width) {
    // Check if the constant has been created yet
    size_t zlid = getConstLID(0, width);
    if (zlid != noLID)
      return zlid;

    // Retrieve the lid associated with the sort (sid)
    size_t sid = sortToLIDMap.at(width);

    // Associate an lid to the new constant
    size_t constlid = setConstLID(0, width);

    // Build and return the zero btor instruction
    os << indent << constlid << " "
       << "zero"
       << " " << sid << "\n";
    return constlid;
  }

  // Generates an init statement, which allows for the use of initial values
  // operands in compreg registers
  void genInit(Operation *reg, Value initVal, int64_t width) {
    // Retrieve the various identifiers we require for this
    size_t regLID = getOpLID(reg);
    size_t sid = sortToLIDMap.at(width);
    size_t initValLID = getOpLID(initVal);

    // Build and emit the string (the lid here doesn't need to be associated
    // to an op as it won't be used)
    os << indent << lid++ << " "
       << "init"
       << " " << sid << " " << regLID << " " << initValLID << "\n";
  }

  // Generates a binary operation instruction given an op name, two operands
  // and a result width.
  void genBinOp(StringRef inst, Operation *binop, Value op1, Value op2,
                size_t width) {
    // TODO: adding support for most variadic ops shouldn't be too hard
    // if (binop->getNumOperands() != 2) {
    //   binop->emitError("variadic operations not are not currently supported");
    //   return;
    // }

    // Set the LID for this operation
    size_t opLID = getOpLID(binop);

    // Find the sort's lid
    size_t sid = sortToLIDMap.at(width);

    // Assuming that the operands were already emitted
    // Find the LIDs associated to the operands
    size_t op1LID = getOpLID(op1);
    size_t op2LID = getOpLID(op2);

    // Build and return the string
    os << indent << opLID << " " << inst << " " << sid << " " << op1LID
       << " " << op2LID;
    emitOptionalName(getOpName(binop));
    os << "\n";

    // Handle variadic operand case where there may be more than to operads to a binop
    unsigned num_operands = binop->getNumOperands();
    unsigned current_op = 2;
    while (current_op < num_operands) {
      Value operand = binop->getOperand(current_op);
      size_t operandLID = getOpLID(operand);
      size_t new_op_lid = setOpLID(binop);
      os << indent << new_op_lid << " " << inst << " " << sid << " " << opLID
         << " " << operandLID;
      emitOptionalName(getOpName(binop));
      os << "\n";
      opLID = new_op_lid;
      current_op++;
    }
  }

  void genConcat(Operation *op, ValueRange operands) {
    assert(operands.size() >= 2 && "concat requires at least two operands");

    size_t accLID = getOpLID(operands.front());
    int64_t accWidth = hw::getBitWidth(operands.front().getType());
    StringRef opName = getOpName(op);

    for (size_t i = 1, e = operands.size(); i != e; ++i) {
      Value operand = operands[i];
      size_t operandLID = getOpLID(operand);
      int64_t operandWidth = hw::getBitWidth(operand.getType());
      int64_t newWidth = accWidth + operandWidth;
      genSort("bitvec", newWidth);
      size_t sid = sortToLIDMap.at(newWidth);
      bool isLast = i + 1 == e;
      size_t resultLID = isLast ? getOpLID(op) : lid++;

      os << indent << resultLID << " concat " << sid << " " << accLID << " "
         << operandLID;
      if (isLast)
        emitOptionalName(opName);
      os << "\n";

      accLID = resultLID;
      accWidth = newWidth;
    }
  }

  // Generates a slice instruction given an operand, the lowbit, and the width
  void genSlice(Operation *srcop, Value op0, size_t lowbit, int64_t width) {    // Assign a LID to this operation
    size_t opLID = getOpLID(srcop);

    // Find the sort's associated lid in order to use it in the instruction
    size_t sid = sortToLIDMap.at(width);

    // Assuming that the operand has already been emitted
    // Find the LID associated to the operand
    size_t op0LID = getOpLID(op0);

    // Build and return the slice instruction
    os << indent << opLID << " "
       << "slice"
       << " " << sid << " " << op0LID << " " << (lowbit + width - 1) << " "
       << lowbit;
    emitOptionalName(getOpName(srcop));
    os << "\n";
  }

  // Generates a constant declaration given a value, a width and a name
  void genUnaryOp(Operation *srcop, Operation *op0, StringRef inst,
                  size_t width) {
    // Register the source operation with the current line id
    size_t opLID = getOpLID(srcop);

    // Retrieve the lid associated with the sort (sid)
    size_t sid = sortToLIDMap.at(width);

    // Assuming that the operand has already been emitted
    // Find the LID associated to the operand
    size_t op0LID = getOpLID(op0);

    os << indent << opLID << " " << inst << " " << sid << " " << op0LID;
    emitOptionalName(getOpName(srcop));
    os << "\n";
  }

  // Generates a constant declaration given a value, a width and a name and
  // returns the LID associated to it
  void genUnaryOp(Operation *srcop, Value op0, StringRef inst, size_t width) {
    genUnaryOp(srcop, op0.getDefiningOp(), inst, width);
  }

  // Generates a constant declaration given a operand lid, a width and a name
  size_t genUnaryOp(size_t op0LID, StringRef inst, size_t width) {
    // Register the source operation with the current line id
    size_t curLid = lid++;

    // Retrieve the lid associated with the sort (sid)
    size_t sid = sortToLIDMap.at(width);

    os << indent << curLid << " " << inst << " " << sid << " " << op0LID << "\n";
    return curLid;
  }

  // Generates a constant declaration given a value, a width and a name
  size_t genUnaryOp(Operation *op0, StringRef inst, size_t width) {
    return genUnaryOp(getOpLID(op0), inst, width);
  }

  // Generates a constant declaration given a value, a width and a name and
  // returns the LID associated to it
  size_t genUnaryOp(Value op0, StringRef inst, size_t width) {
    return genUnaryOp(getOpLID(op0), inst, width);
  }

  // Generate a btor2 assertion given an assertion operation
  // Note that a predicate inversion must have already been generated at this
  // point
  void genBad(Operation *assertop) {
    // Start by finding the expression lid
    size_t assertLID = getOpLID(assertop);
    genBad(assertLID);
  }

  // Generate a btor2 assertion given an assertion operation's LID
  // Note that a predicate inversion must have already been generated at this
  // point
  void genBad(size_t assertLID) {
    // Build and return the btor2 string
    // Also update the lid as this instruction is not associated to an mlir op
    os << indent << lid++ << " "
       << "bad"
       << " " << assertLID << "\n";
  }

  // Generate a btor2 constraint given an expression from an assumption
  // operation
  void genConstraint(Value expr) {
    // Start by finding the expression lid
    size_t exprLID = getOpLID(expr);

    genConstraint(exprLID);
  }

  // Generate a btor2 constraint given an expression from an assumption
  // operation
  void genConstraint(size_t exprLID) {
    // Build and return the btor2 string
    // Also update the lid as this instruction is not associated to an mlir op
    os << lid++ << " "
       << "constraint"
       << " " << exprLID << "\n";
  }

  // Generate an ite instruction (if then else) given a predicate, two values
  // and a res width
  void genIte(Operation *srcop, Value cond, Value t, Value f, int64_t width) {
    // Retrieve the operand lids, assuming they were emitted
    size_t condLID = getOpLID(cond);
    size_t tLID = getOpLID(t);
    size_t fLID = getOpLID(f);

    genIte(srcop, condLID, tLID, fLID, width);
  }

  // Generate an ite instruction (if then else) given a predicate, two values
  // and a res width
  void genIte(Operation *srcop, size_t condLID, size_t tLID, size_t fLID,
              int64_t width) {
    genIte(getOpLID(srcop), getOpName(srcop), condLID, tLID, fLID, width);
  }

  // Generate an ite instruction (if then else) given explicit result and
  // operand line IDs.
  void genIte(size_t opLID, StringRef name, size_t condLID, size_t tLID,
              size_t fLID, int64_t width) {
    // Register the source operation with the current line id
    size_t sid = sortToLIDMap.at(width);

    // Build and return the ite instruction
    os << indent << opLID << " "
       << "ite"
       << " " << sid << " " << condLID << " " << tLID << " " << fLID;
    emitOptionalName(name);
    os << "\n";
  }

  // Generate a logical implication given a lhs and a rhs
  size_t genImplies(Operation *srcop, Value lhs, Value rhs) {
    // Retrieve LIDs for the lhs and rhs
    size_t lhsLID = getOpLID(lhs);
    size_t rhsLID = getOpLID(rhs);

    return genImplies(srcop, lhsLID, rhsLID);
  }

  // Generate a logical implication given a lhs and a rhs
  size_t genImplies(Operation *srcop, size_t lhsLID, size_t rhsLID) {
    // Register the source operation with the current line id
    size_t opLID = getOpLID(srcop);
    return genImplies(opLID, lhsLID, rhsLID);
  }

  size_t genImplies(size_t opLID, size_t lhsLID, size_t rhsLID) {
    // Retrieve the lid associated with the sort (sid)
    size_t sid = sortToLIDMap.at(1);
    // Build and emit the implies operation
    os << indent << opLID << " "
       << "implies"
       << " " << sid << " " << lhsLID << " " << rhsLID << "\n";
    return opLID;
  }

  // Generates a state instruction given a width and a name
  void genState(Operation *srcop, int64_t width, StringRef name) {
    // Register the source operation with the current line id
    size_t opLID = getOpLID(srcop);

    // Retrieve the lid associated with the sort (sid)
    size_t sid = sortToLIDMap.at(width);

    // Build and return the state instruction
    os << indent << opLID << " "
       << "state"
       << " " << sid << " " << name << "\n";
  }

  // Generates a next instruction, given a width, a state LID, and a next
  // value LID
  void genNext(Value next, Operation *reg, int64_t width) {
    // Retrieve the lid associated with the sort (sid)
    size_t sid = sortToLIDMap.at(width);

    // Retrieve the LIDs associated to reg and next
    size_t regLID = getOpLID(reg);
    size_t nextLID = getOpLID(next);

    // Build and return the next instruction
    // Also update the lid as this instruction is not associated to an mlir op
    os << indent << lid++ << " "
       << "next"
       << " " << sid << " " << regLID << " " << nextLID << "\n";
  }

  // Verifies that the sort required for the given operation's btor2 emission
  // has been generated
  int64_t requireSort(mlir::Type type) {
    // Start by figuring out what sort needs to be generated
    int64_t width = hw::getBitWidth(type);

    // Sanity check: getBitWidth can technically return -1 it is a type with
    // no width (like a clock). This shouldn't be allowed as width is required
    // to generate a sort
    assert(width != noWidth);

    // Generate the sort regardles of resulting width (nothing will be added
    // if the sort already exists)
    genSort("bitvec", width);
    return width;
  }

  // Generates the transitions required to finalize the register to state
  // transition system conversion
  void finalizeRegVisit(Operation *op) {
    int64_t width;
    Value next, reset, resetVal;

    // Extract the operands depending on the register type
    if (auto reg = dyn_cast<seq::CompRegOp>(op)) {
      width = hw::getBitWidth(reg.getType());
      next = reg.getInput();
      reset = reg.getReset();
      resetVal = reg.getResetValue();
    } else if (auto reg = dyn_cast<seq::FirRegOp>(op)) {
      width = hw::getBitWidth(reg.getType());
      next = reg.getNext();
      reset = reg.getReset();
      resetVal = reg.getResetValue();
    } else {
      op->emitError("Invalid register operation !");
      return;
    }

    genSort("bitvec", width);

    // Next should already be associated to an LID at this point
    // As we are going to override it, we need to keep track of the original
    // instruction
    size_t nextLID = noLID;

    nextLID = getOpLID(next);

    // Check if the register has a reset
    if (reset) {
      size_t resetValLID = noLID;
      size_t resetLID = getOpLID(reset);

      // Check for a reset value, if none exists assume it's zero
      if (resetVal)
        resetValLID = getOpLID(resetVal);
      else
        resetValLID = genZero(width);

      // Sanity check: at this point the next operation should have had it's
      // btor2 counterpart emitted if not then something terrible must have
      // happened.
      assert(nextLID != noLID);
      assert(resetLID != noLID);
      assert(resetValLID != noLID);

      size_t mergedNextLID;
      StringRef mergedNextName = getValueName(next);
      if (auto *nextOp = next.getDefiningOp()) {
        mergedNextLID = setOpLID(nextOp);
      } else {
        mergedNextLID = setValueLID(next);
      }

      // Generate the ite for the register update reset condition
      // i.e. reg <= reset ? 0 : next
      genIte(mergedNextLID, mergedNextName, resetLID, resetValLID, nextLID,
             width);
    } else {
      // Sanity check: next should have been assigned
      if (nextLID == noLID) {
        op->emitError("Register input does not point to a valid value!");
        return;
      }
    }

    // Finally generate the next statement
    genNext(next, op, width);
  }

public:
  /// Visitor Methods used later on for pattern matching

  // Visitor for the inputs of the module.
  // This will generate additional sorts and input declaration explicitly for
  // btor2 Note that outputs are ignored in btor2 as they do not contribute to
  // the final assertions
  void visit(hw::PortInfo &port) {
    // Separate the inputs from outputs and generate the first btor2 lines for
    // input declaration We only consider ports with an explicit bit-width (so
    // ignore clocks and immutables)
    if (port.isInput() && !isa<seq::ClockType, seq::ImmutableType>(port.type)) {
      // Generate the associated btor declaration for the inputs
      StringRef iName = port.getName();

      // Guarantees that a sort will exist for the generation of this port's
      // translation into btor2
      int64_t w = requireSort(port.type);

      // Save lid for later
      size_t inlid = lid;

      // Record the defining operation's line ID (the module itself in the
      // case of ports)
      inputLIDs[port.argNum] = lid;

      // Increment the lid to keep it unique
      lid++;

      genInput(inlid, w, iName);

      // Record the port in the module ports map
      modulePortLIDs[currentModule][port.getId()] = inlid;

    } else if (port.isOutput() && !isa<seq::ClockType, seq::ImmutableType>(port.type)) {
      // Save the output for later emission
      outputs.push_back(port.getName());
    }
  }

  // Emits the associated btor2 operation for a constant. Note that for
  // simplicity, we will only emit `constd` in order to avoid bit-string
  // conversions
  void visitTypeOp(hw::ConstantOp op) {
    // Make sure the constant hasn't already been created
    if (handledOps.contains(op))
      return;

    // Make sure that a sort has been created for our operation
    int64_t w = requireSort(op.getType());

    // Prepare for for const generation by extracting the const value and
    // generting the btor2 string
    genConst(op.getValue(), w, op);
  }

  void visitBitcastOp(Operation* op) {
    // Get the first operand
    Value op0 = op->getOperand(0);

    size_t lid = getOpLID(op0);

    // Set the current lid to the same lid
    opLIDMap[op] = lid;
  }

  // Handle outputs
  void visitHWOutput(Operation* op) {
    // Iterate over all the operands of op
    unsigned num_operands = op->getNumOperands();
    unsigned current_op = 0;
    while (current_op < num_operands) {
      Value operand = op->getOperand(current_op);
      size_t operandLID = getOpLID(operand);
      size_t new_lid = lid++;
      StringRef name = outputs[current_op];
      auto portLookupInfo = modulePortLookupInfo.at(currentModule);
      auto outputPortIndex = portLookupInfo.getOutputPortIndex(name);
      if (succeeded(outputPortIndex)) {
        modulePortLIDs[currentModule][outputPortIndex.value()] = new_lid;
      } else {
        op->emitError("Output port '" + name.str() + "' not found in module ports!");
      }
      os << indent << new_lid << " " << "output" << " " << operandLID << " " << name << "\n";
      current_op++;
    }
  }

  // Wires should have been removed in PrepareForFormal
  void visit(hw::WireOp op) {
    op->emitError("Wires are not supported in btor!");
    return signalPassFailure();
  }

  void visitTypeOp(Operation *op) { visitInvalidTypeOp(op); }

  // Handles non-hw operations
  void visitInvalidTypeOp(Operation *op) {
    // Try comb ops
    dispatchCombinationalVisitor(op);
  }

  // Binary operations are all emitted the same way, so we can group them into
  // a single method.
  template <typename Op>
  void visitBinOp(Op op, StringRef inst) {
    // Generete the sort
    int64_t w = requireSort(op.getType());

    // Start by extracting the operands
    Value op1 = op.getOperand(0);
    Value op2 = op.getOperand(1);

    // Generate the line
    genBinOp(inst, op, op1, op2, w);
  }

  // Visitors for the binary ops
  void visitComb(comb::AddOp op) { visitBinOp(op, "add"); }
  void visitComb(comb::SubOp op) { visitBinOp(op, "sub"); }
  void visitComb(comb::MulOp op) { visitBinOp(op, "mul"); }
  void visitComb(comb::DivSOp op) { visitBinOp(op, "sdiv"); }
  void visitComb(comb::DivUOp op) { visitBinOp(op, "udiv"); }
  void visitComb(comb::ModSOp op) { visitBinOp(op, "smod"); }
  void visitComb(comb::ShlOp op) { visitBinOp(op, "sll"); }
  void visitComb(comb::ShrUOp op) { visitBinOp(op, "srl"); }
  void visitComb(comb::ShrSOp op) { visitBinOp(op, "sra"); }
  void visitComb(comb::AndOp op) { visitBinOp(op, "and"); }
  void visitComb(comb::OrOp op) { visitBinOp(op, "or"); }
  void visitComb(comb::XorOp op) { visitBinOp(op, "xor"); }
  void visitComb(comb::ConcatOp op) {
    requireSort(op.getType());
    genConcat(op, op.getOperands());
  }

  // Extract ops translate to a slice operation in btor2 in a one-to-one
  // manner
  void visitComb(comb::ExtractOp op) {
    int64_t w = requireSort(op.getType());

    // Start by extracting the necessary information for the emission (i.e.
    // operand, low bit, ...)
    Value op0 = op.getOperand();
    size_t lb = op.getLowBit();

    // Generate the slice instruction
    genSlice(op, op0, lb, w);
  }

  // Btor2 uses similar syntax as hw for its comparisons
  // So we simply need to emit the cmpop name and check for corner cases
  // where the namings differ.
  void visitComb(comb::ICmpOp op) {
    Value lhs = op.getOperand(0);
    Value rhs = op.getOperand(1);

    // Extract the predicate name (assuming that its a valid btor2
    // predicate)
    StringRef pred = stringifyICmpPredicate(op.getPredicate());

    // Check for special cases where hw doesn't align with btor syntax
    if (pred == "ne")
      pred = "neq";
    else if (pred == "ule")
      pred = "ulte";
    else if (pred == "sle")
      pred = "slte";
    else if (pred == "uge")
      pred = "ugte";
    else if (pred == "sge")
      pred = "sgte";

    // Width of result is always 1 for comparison
    genSort("bitvec", 1);

    // With the special cases out of the way, the emission is the same as that
    // of a binary op
    genBinOp(pred, op, lhs, rhs, 1);
  }

  // Muxes generally convert to an ite statement
  void visitComb(comb::MuxOp op) {
    // Extract predicate, true and false values
    Value pred = op.getCond();
    Value tval = op.getTrueValue();
    Value fval = op.getFalseValue();

    // We assume that both tval and fval have the same width
    // This width should be the same as the output width
    int64_t w = requireSort(op.getType());

    // Generate the ite instruction
    genIte(op, pred, tval, fval, w);
  }

  // Replicate operation is emitted as a sign extension
  void visitComb(comb::ReplicateOp op) {
    Value op0 = op.getOperand();
    size_t multiple = op.getMultiple();
    int64_t current_width = hw::getBitWidth(op0.getType());
    
    // TODO: sext only works when a single bit is being replicated to 'n' bit. Otherwise,
    // we may need to do a more expensive concat operations to generate the result.
    // AFAICT, replicate is only emitted for sign-extension, but this assert should act as a guard
    // to prevent future bugs.
    // assert(current_width == 1 && "Only single bit replication is supported");

    // The new width is the current width times the multiple
    int64_t w = current_width * multiple;
    genSort("bitvec", w);

    size_t curLID = getOpLID((Operation *)op);
    size_t op0LID = getOpLID(op0);
    size_t wLID = getSortLID(w);
    StringRef opName = getOpName((Operation *)op);

    if (current_width == 1) {
      // NOTE: BTOR takes as the third argument, the integer, the amount to extend by. 
      // Not the final width after extension, therefore, we need to subtract the current width.
      os << indent << curLID << " "
         << "sext"
         << " " << wLID << " " << op0LID << " " << w - current_width;
      emitOptionalName(opName);
      os << "\n";
    } else {
      if (multiple == 1) {
        opLIDMap[(Operation *)op] = op0LID;
        return;
      }
      size_t prevLID = op0LID;
      int64_t prevWidth = current_width;
      for (size_t i = 1; i < multiple; ++i) {
        int64_t newWidth = prevWidth + current_width;
        genSort("bitvec", newWidth);
        size_t newSortLID = getSortLID(newWidth);
        bool isLast = i + 1 == multiple;
        size_t resultLID = isLast ? curLID : lid++;

        os << indent << resultLID << " "
           << "concat"
           << " " << newSortLID << " " << prevLID << " " << op0LID;
        if (isLast)
          emitOptionalName(opName);
        os << "\n";

        prevLID = resultLID;
        prevWidth = newWidth;
      }
    }
  }

  void visitComb(Operation *op) { visitInvalidComb(op); }

  // Try sv ops when comb is done
  void visitInvalidComb(Operation *op) { dispatchSVVisitor(op); }

  // Assertions are negated then converted to a btor2 bad instruction
  void visitSV(sv::AssertOp op) {
    // Expression is what we will try to invert for our assertion
    Value expr = op.getExpression();

    // This sort is for assertion inversion and potential implies
    genSort("bitvec", 1);

    // Check for an overaching enable
    // In our case the sv.if operation will probably only be used when
    // conditioning an sv.assert on an enable signal. This means that
    // its condition is probably used to imply our assertion
    if (auto ifop = dyn_cast<sv::IfOp>(((Operation *)op)->getParentOp())) {
      Value en = ifop.getOperand();

      // Generate the implication
      genImplies(ifop, en, expr);

      // Generate the implies inversion
      genUnaryOp(op, ifop, "not", 1);
    } else {
      // Generate the expression inversion
      genUnaryOp(op, expr, "not", 1);
    }

    // Generate the bad btor2 instruction
    genBad(op);
  }
  // Assumptions are converted to a btor2 constraint instruction
  void visitSV(sv::AssumeOp op) {
    // Extract the expression that we want our constraint to be about
    Value expr = op.getExpression();
    genConstraint(expr);
  }

  void visitSV(Operation *op) { visitInvalidSV(op); }

  // Once SV Ops are visited, we need to check for seq ops
  void visitInvalidSV(Operation *op) { dispatchVerifVisitor(op); }

  template <typename Op>
  void visitAssertLike(Op op) {
    // Expression is what we will try to invert for our assertion
    Value prop = op.getProperty();
    Value en = op.getEnable();

    // This sort is for assertion inversion and potential implies
    genSort("bitvec", 1);

    size_t assertLID = noLID;
    // Check for a related enable signal
    if (en) {
      // Generate the implication
      genImplies(op, en, prop);

      // Generate the implies inversion
      assertLID = genUnaryOp(op, "not", 1);
    } else {
      // Generate the expression inversion
      assertLID = genUnaryOp(prop.getDefiningOp(), "not", 1);
    }

    // Generate the bad btor2 instruction
    genBad(assertLID);
  }

  template <typename Op>
  void visitAssumeLike(Op op) {
    // Expression is what we will try to invert for our assertion
    Value prop = op.getProperty();
    Value en = op.getEnable();

    size_t assumeLID = getOpLID(prop);
    // Check for a related enable signal
    if (en) {
      // This sort is for assertion inversion and potential implies
      genSort("bitvec", 1);
      // Generate the implication
      assumeLID = genImplies(op, en, prop);
    }

    // Generate the bad btor2 instruction
    genConstraint(assumeLID);
  }

  // Folds the enable signal into the property and converts the result into a
  // bad instruction.
  void visitVerif(verif::AssertOp op) { visitAssertLike(op); }
  void visitVerif(verif::ClockedAssertOp op) { visitAssertLike(op); }

  // Fold the enable into the property and convert the assumption into a
  // constraint instruction.
  void visitVerif(verif::AssumeOp op) { visitAssumeLike(op); }
  void visitVerif(verif::ClockedAssumeOp op) { visitAssumeLike(op); }

  void visitUnhandledVerif(Operation *op) {
    op->emitError("not supported in btor2!");
    return signalPassFailure();
  }

  void visitInvalidVerif(Operation *op) { visit(op); }

  // Seq operation visitor, that dispatches to other seq ops
  // Also handles all remaining operations that should be explicitly ignored
  void visit(Operation *op) {
    // Typeswitch is used here because other seq types will be supported
    // like all operations relating to memories and CompRegs
    TypeSwitch<Operation *, void>(op)
        .Case<seq::FirRegOp, seq::CompRegOp>([&](auto expr) { visit(expr); })
        .Default([&](auto expr) { visitUnsupportedOp(op); });
  }

  // Firrtl registers generate a state instruction
  // The final update is also used to generate a set of next btor
  // instructions
  void visit(seq::FirRegOp reg) {
    // Start by retrieving the register's name and width
    StringRef regName = reg.getName();
    int64_t w = requireSort(reg.getType());

    // Generate state instruction (represents the register declaration)
    genState(reg, w, regName);

    // Record the operation for future `next` instruction generation
    // This is required to model transitions between states (i.e. how a
    // register's value evolves over time)
    regOps.push_back(reg);
  }

  // Compregs behave in a similar way as firregs for btor2 emission
  void visit(seq::CompRegOp reg) {
    // Start by retrieving the register's name and width
    StringRef regName = reg.getName().value();
    int64_t w = requireSort(reg.getType());

    // Check for initial values which must be emitted before the state in
    // btor2
    auto init = reg.getInitialValue();

    // If there's an initial value, we need to generate a constant for the
    // initial value, then declare the state, then generate the init statement
    // (BTOR2 parsers are picky about it being in this order)
    if (init) {
      if (!init.getDefiningOp<seq::InitialOp>()) {
        reg->emitError(
            "Initial value must be emitted directly by a seq.initial op");
        return;
      }
      // Check that the initial value is a non-null constant
      auto initialConstant = circt::seq::unwrapImmutableValue(init)
                                 .getDefiningOp<hw::ConstantOp>();
      if (!initialConstant)
        reg->emitError("initial value must be constant");

      // Visit the initial Value to generate the constant
      dispatchTypeOpVisitor(initialConstant);

      // Add it to the list of visited operations
      handledOps.insert(initialConstant);

      // Now we can declare the state
      genState(reg, w, regName);

      // Finally generate the init statement
      genInit(reg, initialConstant, w);
    } else {
      // Just generate state instruction (represents the register declaration)
      genState(reg, w, regName);
    }

    // Record the operation for future `next` instruction generation
    // This is required to model transitions between states (i.e. how a
    // register's value evolves over time)
    regOps.push_back(reg);
  }

  // Ignore all other explicitly mentionned operations
  // ** Purposefully left empty **
  void ignore(Operation *op) {}

  // Tail method that handles all operations that weren't handled by previous
  // visitors. Here we simply make the pass fail or ignore the op
  void visitUnsupportedOp(Operation *op) {
    // Check for explicitly ignored ops vs unsupported ops (which cause a
    // failure)
    TypeSwitch<Operation *, void>(op)
        // All explicitly ignored operations are defined here
        .Case<sv::MacroDefOp, sv::MacroDeclOp, sv::VerbatimOp,
              sv::VerbatimExprOp, sv::VerbatimExprSEOp, sv::IfOp, sv::IfDefOp,
              sv::IfDefProceduralOp, sv::AlwaysOp, sv::AlwaysCombOp,
              seq::InitialOp, sv::AlwaysFFOp, seq::FromClockOp, seq::InitialOp,
              seq::YieldOp, hw::HWModuleOp>(
            [&](auto expr) { ignore(op); })
        // HW Output
        .Case<hw::OutputOp>([&](auto expr) { visitHWOutput(op); })
        .Case<hw::BitcastOp>([&](auto expr) { visitBitcastOp(op); })
        // Make sure that the design only contains one clock
        .Case<seq::FromClockOp>([&](auto expr) {
          if (++nclocks > 1UL) {
            op->emitOpError("Mutli-clock designs are not supported!");
            return signalPassFailure();
          }
        })

        // Anything else is considered unsupported and might cause a wrong
        // behavior if ignored, so an error is thrown
        .Default([&](auto expr) {
          op->emitOpError("is an unsupported operation");
          return signalPassFailure();
        });
  }
};
} // end anonymous namespace

void ConvertHWToBTOR2PPPass::runOnOperation() {
  // Btor2 does not have the concept of modules or module
  // hierarchies, so we assume that no nested modules exist at this point.
  // This greatly simplifies translation.

  /*
  TODO:
  - For each module, store the inputs and outputs in the maps above (done)
    - For inputs/outputs, in a separate map, map the operation/something to the LID for that module's input/output (done)
  - If a module has dependencies, create an instance of the module using the inst instruction (done)
  - For all referenced inputs/outputs, reference them with ref, then write set/get btor instructions to get that to work (done)
  - Handle outputs (done)
  - Write next for each register (done)
  - Test your changes
  */

  // Firstly, collect all modules and their dependencies
  getOperation().walk([&](hw::HWModuleOp module){
    moduleMap[module.getName()] = module;

    module.walk([&](hw::InstanceOp inst){
      moduleDeps[module].push_back(inst);
    });
  });

  // Print module map and dependencies
  // os << "; ==== Module Map ====\n";
  // for (auto &entry : moduleMap) {
  //   os << "; Module '" << entry.first << "'\n";
  // }
  
  // os << ";\n; ==== Module Dependencies ====\n";
  // for (auto &entry : moduleDeps) {
  //   auto module = cast<hw::HWModuleOp>(entry.first);
  //   os << "; Module '" << module.getName() << "' contains instances:\n";
  //   for (auto inst : entry.second) {
  //     os << ";   - Instance '" << inst->getName() << "' of module '" 
  //        << inst.getModuleName() << "'\n";
  //   }
  // }
  // os << ";\n";

  for (auto module : moduleMap) {
    auto moduleOp = llvm::cast<hw::HWModuleOp>(module.second);
    hw::ModulePortInfo allModulePorts = hw::ModulePortInfo(moduleOp.getPortList());
    modulePorts.insert({moduleOp, allModulePorts});
    modulePortLookupInfo.insert({moduleOp, ModulePortLookupInfo(module.second->getContext(), allModulePorts)});
  }

  // print modulePorts
  // os << "; ==== Module Ports ====\n";
  // for (auto &entry : modulePorts) {
  //   auto hwModule = cast<hw::HWModuleOp>(entry.first);
  //   ModulePortInfo modulePorts = entry.second;
  //   os << "; Module '" << hwModule.getName() << "'\n";

  //   for (auto port : modulePorts.getInputs()) {
  //     os << ";   - Input Port '" << port.getName() << "' with id '" << port.getId() << "'\n";
  //   }

  //   os << "\n";

  //   for (auto port : modulePorts.getOutputs()) {
  //     os << ";   - Output Port '" << port.getName() << "' with id '" << port.getId() << "'\n";
  //   }

  //   os << "\n";

  // }

  auto &instanceGraph = getAnalysis<hw::InstanceGraph>();
  DenseSet<Operation *> handled;

  for (auto *startNode : instanceGraph) {
    // Reason for doing it post order is to handle leaves first (so we're able to reference them in upper level modules)
    for (igraph::InstanceGraphNode *node : llvm::post_order(startNode)) {
      // Only visit each module once in your graph traversal
      if (!handled.insert(node->getModule().getOperation()).second)
        continue;
      
      auto *currentOperation = node->getModule().getOperation();
      auto module = llvm::cast<hw::HWModuleOp>(node->getModule().getOperation());

      // Print out that we're at the start of a file for file parsing purposes later
      os << "; ==== Module '" << module.getModuleName() << "' ====\n";
      
      // Output includes for a module
      auto &deps = moduleDeps[module];
      std::set<std::string> handledIncludes;
      for (auto inst : deps) {
        std::string depModuleName = inst.getModuleName().str();
        std::string depFilePath = (depModuleName + ".btor2pp");
        if (depModuleName != module.getModuleName().str() && handledIncludes.insert(depModuleName).second) {
          os << "include " << depFilePath << "\n";
        }
      }

      os << "\n";

      // Emit Module Block (start)
      os << "module " << module.getModuleName() << " {\n";

      lid = 1;
      currentModule = currentOperation;
      
      // Start by extracting inputs and generating appropriate instructions (and pre-processing outputs)
      for (auto &port : module.getPortList()) {
        visit(port);
      }

      // Previsit all registers in the module in order to avoid dependency cycles
      module.walk([&](Operation *op) {
        TypeSwitch<Operation *, void>(op)
            .Case<seq::FirRegOp, seq::CompRegOp>([&](auto reg) {
              visit(reg);
              handledOps.insert(op);
            });
      });

      DenseSet<StringRef> handledInstances;
      llvm::StringMap<size_t> portRefLIDs;
      auto getPortRefKey = [](StringRef moduleName, ssize_t portID) {
        return (Twine(moduleName) + ":" + Twine(portID)).str();
      };
      for (auto instance : moduleDeps[module]) {
        // TODO: emit inst instruction
        size_t instanceLID = lid;
        instanceLIDs[instance] = instanceLID;
        StringRef moduleName = instance.getModuleName();

        Operation *moduleOp = moduleMap.at(moduleName);
        auto &portInfo = modulePorts.at(moduleOp);  

        os << indent << instanceLID << " " << "inst " << moduleName << " "
           << instance.getInstanceName() << "\n";

        lid++;
        
        // TODO: emit ref instructions
        if (!handledInstances.contains(moduleName)) {
          for (auto port : portInfo.getInputs()) {
            if (isa<seq::ClockType, seq::ImmutableType>(port.type))
              continue;
            
            size_t refLID = lid;
            size_t portLID = modulePortLIDs[moduleOp][port.getId()];
            lid++;
            os << indent << refLID << " " << "ref " << moduleName << " " << portLID << " " << port.getName() << "\n";
            portRefLIDs[getPortRefKey(moduleName, port.getId())] = refLID;
          }

          for (auto port : portInfo.getOutputs()) {
            if (isa<seq::ClockType, seq::ImmutableType>(port.type))
              continue;

            size_t refLID = lid;
            size_t portLID = modulePortLIDs[moduleOp][port.getId()];
            lid++;
            os << indent << refLID << " " << "ref " << moduleName << " " << portLID << " " << port.getName() << "\n";
            portRefLIDs[getPortRefKey(moduleName, port.getId())] = refLID;
          }

          handledInstances.insert(moduleName);
        }

        // TOOD: emit get instructions for each output port
        unsigned outputIdx = 0;
        for (auto port : portInfo.getOutputs()) {
          if (isa<seq::ClockType, seq::ImmutableType>(port.type)) {
            outputIdx++;
            continue;
          }

          Value result = instance.getResult(outputIdx);

          size_t refLID = portRefLIDs[getPortRefKey(moduleName, port.getId())];
          size_t getLID = lid++;

          os << indent << getLID << " get " << instanceLID << " " << refLID << " " << port.getName() << "\n";

          valueLIDMap[result] = getLID;

          outputIdx++;
        }
        
      }

      module.walk([&](Operation *op) {
        // Handle instances at the end of the pass
        if (isa<hw::InstanceOp>(op)) {
          return;
        }

        // Don't process ops that have already been emitted
        if (handledOps.contains(op))
          return;

        if (isa<hw::OutputOp>(op))
          return;

        // Fill in our worklist
        worklist.insert({op, op->operand_begin()});

        // Process the elements in our worklist
        while (!worklist.empty()) {
          auto &[op, operandIt] = worklist.back();
          if (operandIt == op->operand_end()) {
            // All of the operands have been emitted, it is safe to emit our op
            dispatchTypeOpVisitor(op);

            // Record that our op has been emitted
            handledOps.insert(op);
            worklist.pop_back();
            continue;
          }

          // Send the operands of our op to the worklist in case they are still
          // un-emitted
          Value operand = *(operandIt++);
          auto *defOp = operand.getDefiningOp();

          // Make sure that we don't emit the same operand twice; not Instance
          if (!defOp || handledOps.contains(defOp) || isa<hw::InstanceOp>(defOp))
            continue;

          // This is triggered if our operand is already in the worklist and
          // wasn't handled
          if (!worklist.insert({defOp, defOp->operand_begin()}).second) {
            defOp->emitError("dependency cycle");
            return;
          }
        }
      });

      // handle outputs that weren't handled earlier
      for (auto &op : module.getBodyBlock()->getOperations()) {
        if (auto outputOp = dyn_cast<hw::OutputOp>(&op)) {
          for (auto operand : outputOp.getOperands()) {
            // If the operand is a BlockArgument, its LID should already be assigned in inputLIDs.
            // If the operand is a result of an op, it should have been assigned in opLIDMap.
            // If not, you need to emit/handle it here.
            if (mlir::isa<mlir::BlockArgument>(operand)) {
              continue;
            }
            Operation *defOp = operand.getDefiningOp();
            if (isa_and_nonnull<hw::InstanceOp>(defOp)) {
              continue;
            }
            if (defOp && opLIDMap.find(defOp) == opLIDMap.end()) {
              // Emit/handle the defining op here if it wasn't already handled.
              dispatchTypeOpVisitor(defOp);
              handledOps.insert(defOp);
            }
          }
        }
      }

      // emit set instructions
      for (auto instance : moduleDeps[module]) {
        unsigned operandIdx = 0;
        size_t instanceLID = instanceLIDs[instance];
        StringRef moduleName = instance.getModuleName();

        Operation *moduleOp = moduleMap.at(moduleName);
        auto &portInfo = modulePorts.at(moduleOp);

        for (auto port : portInfo.getInputs()) {
          if (isa<seq::ClockType, seq::ImmutableType>(port.type)) {
            operandIdx++;
            continue;
          }
          
          // Get the value in the parent module connected to this input port
          Value operand = instance.getOperand(operandIdx);

          size_t localLID = getOpLID(operand);

          size_t refLID = portRefLIDs[getPortRefKey(moduleName, port.getId())];

          size_t setLID = lid++;
          os << indent << setLID << " set " << instanceLID << " " << refLID << " " << localLID << " " << port.getName() << "\n";

          operandIdx++;
        }
      }

      // emit outputs
      for (auto &op : module.getBodyBlock()->getOperations()) {
        if (auto outputOp = dyn_cast<hw::OutputOp>(&op)) {
          visitHWOutput(outputOp);
        }
      }

      // Iterate through the registers and generate the `next` instructions (do this after handling instances?)
      for (size_t i = 0; i < regOps.size(); ++i) {
        finalizeRegVisit(regOps[i]);
      }

      // Emit Module Block (end)
      os << "}\n";

      // print modulePortLIDs
      // os << "; ==== Module Port LIDs ====\n";
      // auto &allModulePorts = modulePorts.at(module);
      // auto &currentModulePortLIDs = modulePortLIDs.at(module);
      // for (auto &port : currentModulePortLIDs) {
      //   ssize_t portId = port.first;
      //   size_t portLID = port.second;
      //   const PortInfo* portInfo = findPortById(allModulePorts, portId);
      //   // print if input or output
      //   if (portInfo->isInput())
      //     os << ";   - Input Port '" << portId << "' with LID '" << portLID << "'\n";
      //   else
      //     os << ";   - Output Port '" << portId << "' with LID '" << portLID << "'\n";
      //   if (portInfo)
      //     os << ";   - Port from module ports: '" << portInfo->getName() << "'\n";
      //   else
      //     os << ";   - Port not found in module ports!\n";
      // }
      // os << ";\n";

      // Print out that we're at the end of a file
      os << "; ==== End of Module '" << module.getModuleName() << "' ====\n";

      // Clear all LID data structures to allow for pass reuse
      sortToLIDMap.clear();
      constToLIDMap.clear();
      opLIDMap.clear();
      inputLIDs.clear();
      regOps.clear();
      handledOps.clear();
      worklist.clear();
      outputs.clear();
    }
  }
}

// Constructor with a custom ostream
std::unique_ptr<mlir::Pass>
circt::createConvertHWToBTOR2PPPass(llvm::raw_ostream &os) {
  return std::make_unique<ConvertHWToBTOR2PPPass>(os);
}

// Basic default constructor
std::unique_ptr<mlir::Pass> circt::createConvertHWToBTOR2PPPass() {
  return std::make_unique<ConvertHWToBTOR2PPPass>(llvm::outs());
}

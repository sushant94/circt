//===- HWToBTOR2PP.h - HW to BTOR2++ conversion pass ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares passes which together will convert the HW dialect to a
// state transition system and emit it as a BTOR2++ string.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_CONVERSION_HWTOBTOR2_H
#define CIRCT_CONVERSION_HWTOBTOR2_H

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

namespace mlir {
class Pass;
} // namespace mlir

namespace circt {

#define GEN_PASS_DECL_CONVERTHWTOBTOR2PP
#include "circt/Conversion/Passes.h.inc"

std::unique_ptr<mlir::Pass> createConvertHWToBTOR2PPPass(llvm::raw_ostream &os);
std::unique_ptr<mlir::Pass> createConvertHWToBTOR2PPPass();

} // namespace circt

#endif // CIRCT_CONVERSION_HWTOBTOR2_H

#!/usr/bin/env python3
"""Utilities for recovering BTOR transition systems and building equivalence miters.

This script supports two workflows:

1. Split an existing miter into standalone transition systems.
2. Build a fresh equivalence miter between two standalone transition systems.

The generated equivalence miter compares normalized shared state names and
initializes each compared state pair to the same symbolic initial value. That
avoids the trivial counterexamples caused by independent unconstrained initial
states.
"""

from __future__ import annotations

import argparse
import json
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Set, Tuple


class BtorError(RuntimeError):
    """Raised when a BTOR file cannot be parsed or transformed safely."""


OP_SCHEMA = {
    "sort": "sort",
    "input": "decl",
    "state": "decl",
    "const": "const",
    "consth": "const",
    "output": "root",
    "bad": "root",
    "constraint": "root",
    "fair": "root",
    "justice": "root",
    "init": "init",
    "next": "next",
    "not": "unary",
    "neg": "unary",
    "redand": "unary",
    "redor": "unary",
    "and": "binary",
    "or": "binary",
    "xor": "binary",
    "add": "binary",
    "sub": "binary",
    "mul": "binary",
    "eq": "binary",
    "neq": "binary",
    "ult": "binary",
    "ugt": "binary",
    "ugte": "binary",
    "slt": "binary",
    "sgt": "binary",
    "sll": "binary",
    "srl": "binary",
    "sra": "binary",
    "implies": "binary",
    "ite": "ternary",
    "uext": "extend",
    "sext": "extend",
    "slice": "slice",
    "concat": "binary",
}


def _normalize_name(name: str) -> str:
    normalized = name.replace(".", "_").replace("/", "_")
    if normalized.startswith("in_"):
        normalized = normalized[3:]
    return normalized


def _strip_namespace(name: Optional[str], namespace: str) -> Optional[str]:
    if not name:
        return None
    prefix = f"{namespace}."
    if name.startswith(prefix):
        return name[len(prefix) :]
    return name


def _prefix_name(name: Optional[str], namespace: str) -> Optional[str]:
    if not name:
        return None
    return f"{namespace}.{name}"


def _sanitize_label(value: str) -> str:
    cleaned = value.replace("/", "_").replace(".", "_")
    return cleaned


def _should_ignore_name(name: str, ignore_substrings: Sequence[str]) -> bool:
    return any(fragment in name for fragment in ignore_substrings)


def _find_btormc() -> Optional[str]:
    direct = shutil.which("btormc")
    if direct:
        return direct
    candidates = [
        Path("build/bin/btormc"),
        Path("llvm/build/bin/btormc"),
        Path("build/tools/btormc"),
    ]
    for candidate in candidates:
        if candidate.is_file() and candidate.exists():
            return str(candidate.resolve())
    return None


@dataclass(frozen=True)
class SortSignature:
    kind: str
    args: Tuple[object, ...]

    def to_json(self) -> object:
        if self.kind == "bitvec":
            return {"kind": "bitvec", "width": self.args[0]}
        if self.kind == "array":
            return {
                "kind": "array",
                "index": self.args[0].to_json(),
                "element": self.args[1].to_json(),
            }
        raise BtorError(f"Unsupported sort signature kind: {self.kind}")


@dataclass
class BtorNode:
    nid: int
    op: str
    data: Tuple[str, ...]
    name: Optional[str]

    @property
    def schema(self) -> str:
        try:
            return OP_SCHEMA[self.op]
        except KeyError as exc:
            raise BtorError(f"Unsupported BTOR op '{self.op}'") from exc

    def ref_positions(self) -> Tuple[int, ...]:
        schema = self.schema
        if schema == "sort":
            if self.data[0] == "array":
                return (1, 2)
            return ()
        if schema in {"decl", "const", "root"}:
            return (0,)
        if schema in {"next", "init"}:
            return (0, 1, 2)
        if schema == "unary":
            return (0, 1)
        if schema == "binary":
            return (0, 1, 2)
        if schema == "ternary":
            return (0, 1, 2, 3)
        if schema == "extend":
            return (0, 1)
        if schema == "slice":
            return (0, 1)
        raise BtorError(f"Unexpected schema '{schema}' for op '{self.op}'")

    def refs(self) -> List[int]:
        return [int(self.data[index]) for index in self.ref_positions()]

    def rewrite_data(self, id_map: Dict[int, int]) -> Tuple[str, ...]:
        rewritten = list(self.data)
        for position in self.ref_positions():
            rewritten[position] = str(id_map[int(rewritten[position])])
        return tuple(rewritten)

    def with_name(self, name: Optional[str]) -> "BtorNode":
        return BtorNode(self.nid, self.op, self.data, name)

    def render(self, nid: int, data: Sequence[str], name: Optional[str]) -> str:
        tokens = [str(nid), self.op, *data]
        if name:
            tokens.append(name)
        return " ".join(tokens)


class BtorProgram:
    def __init__(self, path: Path, nodes: Dict[int, BtorNode], order: List[int]):
        self.path = path
        self.nodes = nodes
        self.order = order
        self.next_by_state: Dict[int, int] = {}
        self.init_by_state: Dict[int, int] = {}
        for nid in order:
            node = nodes[nid]
            if node.op == "next":
                self.next_by_state[int(node.data[1])] = nid
            elif node.op == "init":
                self.init_by_state[int(node.data[1])] = nid
        self._sort_cache: Dict[int, SortSignature] = {}

    @classmethod
    def parse(cls, path: Path) -> "BtorProgram":
        nodes: Dict[int, BtorNode] = {}
        order: List[int] = []
        with path.open() as handle:
            for raw_line in handle:
                line = raw_line.split(";", 1)[0].strip()
                if not line:
                    continue
                parts = line.split()
                nid = int(parts[0])
                op = parts[1]
                data, name = cls._parse_fields(op, parts[2:])
                node = BtorNode(nid=nid, op=op, data=data, name=name)
                nodes[nid] = node
                order.append(nid)
        return cls(path=path, nodes=nodes, order=order)

    @staticmethod
    def _parse_fields(op: str, fields: Sequence[str]) -> Tuple[Tuple[str, ...], Optional[str]]:
        schema = OP_SCHEMA.get(op)
        if schema is None:
            raise BtorError(f"Unsupported BTOR op '{op}'")
        if schema == "sort":
            if not fields:
                raise BtorError("Malformed sort node")
            if fields[0] == "bitvec":
                expected = 2
            elif fields[0] == "array":
                expected = 3
            else:
                raise BtorError(f"Unsupported sort kind '{fields[0]}'")
        elif schema in {"decl", "root"}:
            expected = 1
        elif schema == "const":
            expected = 2
        elif schema in {"next", "init"}:
            expected = 3
        elif schema == "unary":
            expected = 2
        elif schema == "binary":
            expected = 3
        elif schema == "ternary":
            expected = 4
        elif schema == "extend":
            expected = 3
        elif schema == "slice":
            expected = 4
        else:
            raise BtorError(f"Unexpected schema '{schema}'")

        if len(fields) == expected:
            return tuple(fields), None
        if len(fields) == expected + 1:
            return tuple(fields[:-1]), fields[-1]
        raise BtorError(f"Unexpected operand count for op '{op}': {' '.join(fields)}")

    def sort_signature(self, sort_id: int) -> SortSignature:
        if sort_id in self._sort_cache:
            return self._sort_cache[sort_id]
        node = self.nodes[sort_id]
        if node.op != "sort":
            raise BtorError(f"Node {sort_id} is not a sort")
        if node.data[0] == "bitvec":
            signature = SortSignature("bitvec", (int(node.data[1]),))
        elif node.data[0] == "array":
            signature = SortSignature(
                "array",
                (
                    self.sort_signature(int(node.data[1])),
                    self.sort_signature(int(node.data[2])),
                ),
            )
        else:
            raise BtorError(f"Unsupported sort kind '{node.data[0]}'")
        self._sort_cache[sort_id] = signature
        return signature

    def node_sort_signature(self, nid: int) -> SortSignature:
        node = self.nodes[nid]
        if node.op == "sort":
            return self.sort_signature(nid)
        if node.schema == "root":
            return self.node_sort_signature(int(node.data[0]))
        if node.schema in {"decl", "const", "next", "init", "unary", "binary", "ternary", "extend", "slice"}:
            return self.sort_signature(int(node.data[0]))
        raise BtorError(f"Cannot determine result sort for node {nid} ({node.op})")

    def named_nodes(self, op: str) -> Dict[str, int]:
        result: Dict[str, int] = {}
        for nid in self.order:
            node = self.nodes[nid]
            if node.op == op and node.name:
                result[node.name] = nid
        return result

    def state_ids(self) -> List[int]:
        return [nid for nid in self.order if self.nodes[nid].op == "state"]

    def input_ids(self) -> List[int]:
        return [nid for nid in self.order if self.nodes[nid].op == "input"]

    def closure_from_roots(self, root_ids: Iterable[int]) -> Set[int]:
        visited: Set[int] = set()
        stack: List[int] = list(root_ids)
        while stack:
            current = stack.pop()
            if current in visited:
                continue
            visited.add(current)
            for ref in self.nodes[current].refs():
                if ref not in visited:
                    stack.append(ref)
        return visited

    def transition_system_closure(self, state_ids: Iterable[int]) -> Set[int]:
        root_ids: Set[int] = set()
        for state_id in state_ids:
            root_ids.add(state_id)
            next_id = self.next_by_state.get(state_id)
            if next_id is None:
                raise BtorError(f"State {state_id} has no matching next node")
            root_ids.add(next_id)
            init_id = self.init_by_state.get(state_id)
            if init_id is not None:
                root_ids.add(init_id)
        return self.closure_from_roots(root_ids)


class BtorWriter:
    def __init__(self) -> None:
        self.lines: List[str] = []
        self.next_id = 1
        self.sort_cache: Dict[SortSignature, int] = {}
        self.bit1_sort = self.add_sort(SortSignature("bitvec", (1,)))

    def add_sort(self, signature: SortSignature) -> int:
        cached = self.sort_cache.get(signature)
        if cached is not None:
            return cached
        nid = self.next_id
        self.next_id += 1
        if signature.kind == "bitvec":
            line = f"{nid} sort bitvec {signature.args[0]}"
        elif signature.kind == "array":
            index_id = self.add_sort(signature.args[0])
            element_id = self.add_sort(signature.args[1])
            line = f"{nid} sort array {index_id} {element_id}"
        else:
            raise BtorError(f"Unsupported sort signature kind '{signature.kind}'")
        self.lines.append(line)
        self.sort_cache[signature] = nid
        return nid

    def add_node(self, op: str, data: Sequence[object], name: Optional[str] = None) -> int:
        nid = self.next_id
        self.next_id += 1
        tokens = [str(nid), op, *[str(token) for token in data]]
        if name:
            tokens.append(name)
        self.lines.append(" ".join(tokens))
        return nid

    def import_nodes(
        self,
        program: BtorProgram,
        selected_ids: Set[int],
        *,
        input_remap: Optional[Dict[int, int]] = None,
        state_name_prefix: Optional[str] = None,
        strip_namespace: Optional[str] = None,
        keep_roots: bool = False,
        skip_init_states: Optional[Set[int]] = None,
    ) -> Dict[int, int]:
        id_map: Dict[int, int] = {}
        for nid in program.order:
            if nid not in selected_ids:
                continue
            node = program.nodes[nid]
            if node.op == "sort":
                signature = program.sort_signature(nid)
                id_map[nid] = self.add_sort(signature)
                continue

            if node.op == "input" and input_remap and nid in input_remap:
                id_map[nid] = input_remap[nid]
                continue

            if (
                node.op == "init"
                and skip_init_states
                and int(node.data[1]) in skip_init_states
            ):
                continue

            rewritten = list(node.data)
            for position in node.ref_positions():
                rewritten[position] = str(id_map[int(rewritten[position])])

            name = node.name
            if strip_namespace:
                name = _strip_namespace(name, strip_namespace)
            if state_name_prefix and node.op == "state":
                name = _prefix_name(name, state_name_prefix)
            elif state_name_prefix and node.op not in {"input", "sort"} and name:
                name = _prefix_name(name, state_name_prefix)

            if node.op in {"bad", "constraint", "fair", "justice", "output"} and not keep_roots:
                continue

            id_map[nid] = self.add_node(node.op, rewritten, name)
        return id_map

    def write_to(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("\n".join(self.lines) + "\n")


def _index_by_normalized_name(
    program: BtorProgram,
    op: str,
    *,
    ignore_substrings: Sequence[str] = (),
) -> Dict[str, Tuple[int, str]]:
    by_name: Dict[str, Tuple[int, str]] = {}
    for nid in program.order:
        node = program.nodes[nid]
        if node.op != op or not node.name:
            continue
        normalized = _normalize_name(node.name)
        if _should_ignore_name(normalized, ignore_substrings):
            continue
        if normalized in by_name:
            raise BtorError(
                f"Normalized {op} name collision '{normalized}' in {program.path}: "
                f"{by_name[normalized][1]} and {node.name}"
            )
        by_name[normalized] = (nid, node.name)
    return by_name


def _ignored_named_nodes(
    program: BtorProgram,
    op: str,
    *,
    ignore_substrings: Sequence[str] = (),
) -> List[Tuple[int, str, str]]:
    ignored: List[Tuple[int, str, str]] = []
    for nid in program.order:
        node = program.nodes[nid]
        if node.op != op or not node.name:
            continue
        normalized = _normalize_name(node.name)
        if _should_ignore_name(normalized, ignore_substrings):
            ignored.append((nid, normalized, node.name))
    return ignored


def _sort_to_json(signature: SortSignature) -> object:
    return signature.to_json()


def _bitvec_width(signature: SortSignature) -> int:
    if signature.kind != "bitvec":
        raise BtorError(f"Expected bitvec sort, got {signature.kind}")
    return int(signature.args[0])


def split_miter(
    input_path: Path,
    left_namespace: str,
    right_namespace: str,
    output_dir: Path,
) -> Dict[str, object]:
    program = BtorProgram.parse(input_path)
    output_dir.mkdir(parents=True, exist_ok=True)

    state_names = program.named_nodes("state")
    left_states = sorted(
        nid for name, nid in state_names.items() if name.startswith(f"{left_namespace}.")
    )
    right_states = sorted(
        nid for name, nid in state_names.items() if name.startswith(f"{right_namespace}.")
    )
    if not left_states or not right_states:
        raise BtorError(
            f"Could not find both '{left_namespace}.' and '{right_namespace}.' state namespaces"
        )

    results = []
    for namespace, state_ids in ((left_namespace, left_states), (right_namespace, right_states)):
        selected = program.transition_system_closure(state_ids)
        selected.update(
            nid for nid in program.input_ids() if program.nodes[nid].name is not None
        )
        writer = BtorWriter()
        writer.import_nodes(
            program,
            selected,
            strip_namespace=namespace,
            keep_roots=False,
        )
        output_path = output_dir / f"{input_path.stem}_{namespace}.btor2"
        writer.write_to(output_path)
        recovered = BtorProgram.parse(output_path)
        results.append(
            {
                "namespace": namespace,
                "path": str(output_path),
                "inputs": len(recovered.input_ids()),
                "states": len(recovered.state_ids()),
                "nexts": len(recovered.next_by_state),
            }
        )

    report = {
        "source": str(input_path),
        "artifacts": results,
        "source_summary": {
            "inputs": len(program.input_ids()),
            "states": len(program.state_ids()),
            "nexts": len(program.next_by_state),
            "bads": sum(1 for nid in program.order if program.nodes[nid].op == "bad"),
            "outputs": sum(1 for nid in program.order if program.nodes[nid].op == "output"),
        },
    }
    return report


def build_equiv_miter(
    left_path: Path,
    right_path: Path,
    output_path: Path,
    *,
    left_label: Optional[str] = None,
    right_label: Optional[str] = None,
    compare_op: str = "state",
    ignore_substrings: Sequence[str] = (),
) -> Dict[str, object]:
    left_program = BtorProgram.parse(left_path)
    right_program = BtorProgram.parse(right_path)

    left_label = left_label or _sanitize_label(left_path.stem)
    right_label = right_label or _sanitize_label(right_path.stem)

    if compare_op not in {"state", "output"}:
        raise BtorError(f"Unsupported compare op '{compare_op}'")

    left_input_index = _index_by_normalized_name(
        left_program, "input", ignore_substrings=ignore_substrings
    )
    right_input_index = _index_by_normalized_name(
        right_program, "input", ignore_substrings=ignore_substrings
    )
    left_state_index = _index_by_normalized_name(
        left_program, "state", ignore_substrings=ignore_substrings
    )
    right_state_index = _index_by_normalized_name(
        right_program, "state", ignore_substrings=ignore_substrings
    )
    left_compare_index = _index_by_normalized_name(
        left_program, compare_op, ignore_substrings=ignore_substrings
    )
    right_compare_index = _index_by_normalized_name(
        right_program, compare_op, ignore_substrings=ignore_substrings
    )
    ignored_left_inputs = _ignored_named_nodes(
        left_program, "input", ignore_substrings=ignore_substrings
    )
    ignored_right_inputs = _ignored_named_nodes(
        right_program, "input", ignore_substrings=ignore_substrings
    )
    ignored_left_states = _ignored_named_nodes(
        left_program, "state", ignore_substrings=ignore_substrings
    )
    ignored_right_states = _ignored_named_nodes(
        right_program, "state", ignore_substrings=ignore_substrings
    )

    matched_inputs = sorted(set(left_input_index) & set(right_input_index))
    unmatched_left_inputs = sorted(set(left_input_index) - set(right_input_index))
    unmatched_right_inputs = sorted(set(right_input_index) - set(left_input_index))
    matched_states = sorted(set(left_state_index) & set(right_state_index))
    unmatched_left_states = sorted(set(left_state_index) - set(right_state_index))
    unmatched_right_states = sorted(set(right_state_index) - set(left_state_index))
    matched_compare = sorted(set(left_compare_index) & set(right_compare_index))
    unmatched_left_compare = sorted(set(left_compare_index) - set(right_compare_index))
    unmatched_right_compare = sorted(set(right_compare_index) - set(left_compare_index))

    writer = BtorWriter()
    left_selected = left_program.transition_system_closure(left_program.state_ids())
    right_selected = right_program.transition_system_closure(right_program.state_ids())
    if compare_op == "output":
        left_selected.update(
            left_program.closure_from_roots(
                left_compare_index[normalized][0] for normalized in matched_compare
            )
        )
        right_selected.update(
            right_program.closure_from_roots(
                right_compare_index[normalized][0] for normalized in matched_compare
            )
        )

    left_input_remap: Dict[int, int] = {}
    right_input_remap: Dict[int, int] = {}
    matched_input_report = []
    unmatched_left_input_report = []
    unmatched_right_input_report = []
    ignored_left_input_report = []
    ignored_right_input_report = []
    ignored_left_state_report = []
    ignored_right_state_report = []

    for normalized in matched_inputs:
        left_id, left_name = left_input_index[normalized]
        right_id, right_name = right_input_index[normalized]
        left_sort = left_program.node_sort_signature(left_id)
        right_sort = right_program.node_sort_signature(right_id)
        if left_sort != right_sort:
            raise BtorError(
                f"Shared input '{normalized}' has mismatched sorts: {left_name} vs {right_name}"
            )
        input_id = writer.add_node(
            "input",
            [writer.add_sort(left_sort)],
            normalized,
        )
        left_input_remap[left_id] = input_id
        right_input_remap[right_id] = input_id
        matched_input_report.append(
            {
                "normalized": normalized,
                "left": left_name,
                "right": right_name,
                "sort": _sort_to_json(left_sort),
            }
        )

    for normalized in unmatched_left_inputs:
        nid, original_name = left_input_index[normalized]
        signature = left_program.sort_signature(int(left_program.nodes[nid].data[0]))
        input_id = writer.add_node(
            "input",
            [writer.add_sort(signature)],
            f"{left_label}.{original_name}",
        )
        left_input_remap[nid] = input_id
        unmatched_left_input_report.append(
            {
                "normalized": normalized,
                "name": original_name,
                "sort": _sort_to_json(signature),
            }
        )

    for normalized in unmatched_right_inputs:
        nid, original_name = right_input_index[normalized]
        signature = right_program.sort_signature(int(right_program.nodes[nid].data[0]))
        input_id = writer.add_node(
            "input",
            [writer.add_sort(signature)],
            f"{right_label}.{original_name}",
        )
        right_input_remap[nid] = input_id
        unmatched_right_input_report.append(
            {
                "normalized": normalized,
                "name": original_name,
                "sort": _sort_to_json(signature),
            }
        )

    for nid, normalized, original_name in ignored_left_inputs:
        signature = left_program.node_sort_signature(nid)
        if signature.kind != "bitvec":
            raise BtorError(f"Ignored input '{original_name}' is not a bitvec and cannot be zeroed")
        zero_id = writer.add_node("consth", [writer.add_sort(signature), "0"])
        left_input_remap[nid] = zero_id
        ignored_left_input_report.append(
            {
                "normalized": normalized,
                "name": original_name,
                "sort": _sort_to_json(signature),
                "replacement": "const_zero",
            }
        )

    for nid, normalized, original_name in ignored_right_inputs:
        signature = right_program.node_sort_signature(nid)
        if signature.kind != "bitvec":
            raise BtorError(f"Ignored input '{original_name}' is not a bitvec and cannot be zeroed")
        zero_id = writer.add_node("consth", [writer.add_sort(signature), "0"])
        right_input_remap[nid] = zero_id
        ignored_right_input_report.append(
            {
                "normalized": normalized,
                "name": original_name,
                "sort": _sort_to_json(signature),
                "replacement": "const_zero",
            }
        )

    init_bindings: Dict[str, Dict[str, object]] = {}
    matched_state_report = []
    ignored_left_state_zero_ids: Dict[int, int] = {}
    ignored_right_state_zero_ids: Dict[int, int] = {}

    for nid, _, original_name in ignored_left_states:
        signature = left_program.node_sort_signature(nid)
        if signature.kind != "bitvec":
            raise BtorError(f"Ignored state '{original_name}' is not a bitvec and cannot be zeroed")
        ignored_left_state_zero_ids[nid] = writer.add_node(
            "consth", [writer.add_sort(signature), "0"]
        )

    for nid, _, original_name in ignored_right_states:
        signature = right_program.node_sort_signature(nid)
        if signature.kind != "bitvec":
            raise BtorError(f"Ignored state '{original_name}' is not a bitvec and cannot be zeroed")
        ignored_right_state_zero_ids[nid] = writer.add_node(
            "consth", [writer.add_sort(signature), "0"]
        )

    for normalized in matched_states:
        left_id, left_name = left_state_index[normalized]
        right_id, right_name = right_state_index[normalized]
        left_sort = left_program.sort_signature(int(left_program.nodes[left_id].data[0]))
        right_sort = right_program.sort_signature(int(right_program.nodes[right_id].data[0]))
        widths_match = left_sort == right_sort
        if not widths_match:
            if left_sort.kind != "bitvec" or right_sort.kind != "bitvec":
                raise BtorError(
                    f"Shared state '{normalized}' has unsupported mismatched sorts: "
                    f"{left_name} vs {right_name}"
                )
            common_init_sort = SortSignature(
                "bitvec",
                (min(_bitvec_width(left_sort), _bitvec_width(right_sort)),),
            )
        else:
            common_init_sort = left_sort

        helper_state_name = f"init_state.{normalized}"
        helper_state_id = writer.add_node(
            "state",
            [writer.add_sort(common_init_sort)],
            helper_state_name,
        )

        left_init_value = helper_state_id
        if _bitvec_width(left_sort) > _bitvec_width(common_init_sort):
            left_init_value = writer.add_node(
                "uext",
                [
                    writer.add_sort(left_sort),
                    helper_state_id,
                    _bitvec_width(left_sort) - _bitvec_width(common_init_sort),
                ],
            )
        right_init_value = helper_state_id
        if _bitvec_width(right_sort) > _bitvec_width(common_init_sort):
            right_init_value = writer.add_node(
                "uext",
                [
                    writer.add_sort(right_sort),
                    helper_state_id,
                    _bitvec_width(right_sort) - _bitvec_width(common_init_sort),
                ],
            )
        init_bindings[normalized] = {
            "left_id": left_id,
            "right_id": right_id,
            "left_sort": left_sort,
            "right_sort": right_sort,
            "common_init_sort": common_init_sort,
            "helper_state_id": helper_state_id,
            "left_value": left_init_value,
            "right_value": right_init_value,
        }
        matched_state_report.append(
            {
                "normalized": normalized,
                "left": left_name,
                "right": right_name,
                "sort": _sort_to_json(left_sort),
                "right_sort": _sort_to_json(right_sort),
                "init_state": helper_state_name,
                "init_sort": _sort_to_json(common_init_sort),
                "widened_compare": not widths_match,
            }
        )

    left_map = writer.import_nodes(
        left_program,
        left_selected,
        input_remap=left_input_remap,
        state_name_prefix=left_label,
        keep_roots=False,
        skip_init_states={nid for nid, _, _ in ignored_left_states},
    )
    right_map = writer.import_nodes(
        right_program,
        right_selected,
        input_remap=right_input_remap,
        state_name_prefix=right_label,
        keep_roots=False,
        skip_init_states={nid for nid, _, _ in ignored_right_states},
    )

    for nid, normalized, original_name in ignored_left_states:
        signature = left_program.node_sort_signature(nid)
        writer.add_node(
            "init",
            [
                writer.add_sort(signature),
                left_map[nid],
                ignored_left_state_zero_ids[nid],
            ],
        )
        ignored_left_state_report.append(
            {
                "normalized": normalized,
                "name": original_name,
                "sort": _sort_to_json(signature),
                "replacement": "const_zero_init",
            }
        )

    for nid, normalized, original_name in ignored_right_states:
        signature = right_program.node_sort_signature(nid)
        writer.add_node(
            "init",
            [
                writer.add_sort(signature),
                right_map[nid],
                ignored_right_state_zero_ids[nid],
            ],
        )
        ignored_right_state_report.append(
            {
                "normalized": normalized,
                "name": original_name,
                "sort": _sort_to_json(signature),
                "replacement": "const_zero_init",
            }
        )

    for normalized in matched_states:
        binding = init_bindings[normalized]
        writer.add_node(
            "next",
            [
                writer.add_sort(binding["common_init_sort"]),
                binding["helper_state_id"],
                binding["helper_state_id"],
            ],
        )
    for normalized in matched_states:
        binding = init_bindings[normalized]
        writer.add_node(
            "init",
            [
                writer.add_sort(binding["left_sort"]),
                left_map[binding["left_id"]],
                binding["left_value"],
            ],
        )
        writer.add_node(
            "init",
            [
                writer.add_sort(binding["right_sort"]),
                right_map[binding["right_id"]],
                binding["right_value"],
            ],
        )

    unmatched_left_state_report = [
        {
            "normalized": normalized,
            "name": left_state_index[normalized][1],
            "sort": _sort_to_json(
                left_program.node_sort_signature(left_state_index[normalized][0])
            ),
        }
        for normalized in unmatched_left_states
    ]
    unmatched_right_state_report = [
        {
            "normalized": normalized,
            "name": right_state_index[normalized][1],
            "sort": _sort_to_json(
                right_program.node_sort_signature(right_state_index[normalized][0])
            ),
        }
        for normalized in unmatched_right_states
    ]

    mismatch_expr: Optional[int] = None
    mismatch_nodes = []
    matched_compare_report = []
    for normalized in matched_compare:
        left_id = left_compare_index[normalized][0]
        right_id = right_compare_index[normalized][0]
        left_sort = left_program.node_sort_signature(left_id)
        right_sort = right_program.node_sort_signature(right_id)
        compare_sort = left_sort
        if compare_op == "output":
            left_expr = left_map[int(left_program.nodes[left_id].data[0])]
            right_expr = right_map[int(right_program.nodes[right_id].data[0])]
        else:
            left_expr = left_map[left_id]
            right_expr = right_map[right_id]
        if left_sort != right_sort:
            compare_sort = SortSignature(
                "bitvec",
                (max(_bitvec_width(left_sort), _bitvec_width(right_sort)),),
            )
            if _bitvec_width(left_sort) < _bitvec_width(compare_sort):
                left_expr = writer.add_node(
                    "uext",
                    [
                        writer.add_sort(compare_sort),
                        left_expr,
                        _bitvec_width(compare_sort) - _bitvec_width(left_sort),
                    ],
                )
            if _bitvec_width(right_sort) < _bitvec_width(compare_sort):
                right_expr = writer.add_node(
                    "uext",
                    [
                        writer.add_sort(compare_sort),
                        right_expr,
                        _bitvec_width(compare_sort) - _bitvec_width(right_sort),
                    ],
                )

        neq_id = writer.add_node(
            "neq",
            [
                writer.bit1_sort,
                left_expr,
                right_expr,
            ],
            f"mismatch.{normalized}",
        )
        mismatch_nodes.append({"normalized": normalized, "node": neq_id})
        if mismatch_expr is None:
            mismatch_expr = neq_id
        else:
            mismatch_expr = writer.add_node(
                "or",
                [writer.bit1_sort, mismatch_expr, neq_id],
            )

        matched_compare_report.append(
            {
                "normalized": normalized,
                "left": left_compare_index[normalized][1],
                "right": right_compare_index[normalized][1],
                "sort": _sort_to_json(left_sort),
                "right_sort": _sort_to_json(right_sort),
                "widened_compare": left_sort != right_sort,
            }
        )

    if mismatch_expr is None:
        raise BtorError(f"No shared {compare_op}s were found for the equivalence miter")

    unmatched_left_compare_report = [
        {
            "normalized": normalized,
            "name": left_compare_index[normalized][1],
            "sort": _sort_to_json(
                left_program.node_sort_signature(left_compare_index[normalized][0])
            ),
        }
        for normalized in unmatched_left_compare
    ]
    unmatched_right_compare_report = [
        {
            "normalized": normalized,
            "name": right_compare_index[normalized][1],
            "sort": _sort_to_json(
                right_program.node_sort_signature(right_compare_index[normalized][0])
            ),
        }
        for normalized in unmatched_right_compare
    ]

    writer.add_node("bad", [mismatch_expr], "equiv.bad")
    writer.write_to(output_path)

    btormc_path = _find_btormc()
    report = {
        "left_design": str(left_path),
        "right_design": str(right_path),
        "output_miter": str(output_path),
        "matched_inputs": matched_input_report,
        "unmatched_left_inputs": unmatched_left_input_report,
        "unmatched_right_inputs": unmatched_right_input_report,
        "ignored_left_inputs": ignored_left_input_report,
        "ignored_right_inputs": ignored_right_input_report,
        "ignored_left_states": ignored_left_state_report,
        "ignored_right_states": ignored_right_state_report,
        "matched_states": matched_state_report,
        "unmatched_left_states": unmatched_left_state_report,
        "unmatched_right_states": unmatched_right_state_report,
        "compare_op": compare_op,
        "ignore_substrings": list(ignore_substrings),
        "matched_compare": matched_compare_report,
        "unmatched_left_compare": unmatched_left_compare_report,
        "unmatched_right_compare": unmatched_right_compare_report,
        "mismatch_nodes": mismatch_nodes,
        "counts": {
            "matched_inputs": len(matched_input_report),
            "unmatched_left_inputs": len(unmatched_left_input_report),
            "unmatched_right_inputs": len(unmatched_right_input_report),
            "ignored_left_inputs": len(ignored_left_input_report),
            "ignored_right_inputs": len(ignored_right_input_report),
            "ignored_left_states": len(ignored_left_state_report),
            "ignored_right_states": len(ignored_right_state_report),
            "matched_states": len(matched_state_report),
            "unmatched_left_states": len(unmatched_left_state_report),
            "unmatched_right_states": len(unmatched_right_state_report),
            "matched_compare": len(matched_compare_report),
            "unmatched_left_compare": len(unmatched_left_compare_report),
            "unmatched_right_compare": len(unmatched_right_compare_report),
            "added_init_inputs": len(matched_state_report),
            "bad_nodes": 1,
        },
        "btormc": {
            "path": btormc_path,
            "suggested_command": [btormc_path or "btormc", str(output_path)],
        },
    }
    return report


def _write_report(path: Path, report: Dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser(description="Recover BTOR designs and build equivalence miters.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    split_parser = subparsers.add_parser("split-miter", help="Split a two-sided BTOR miter into standalone designs")
    split_parser.add_argument("input", type=Path)
    split_parser.add_argument("--left", default="gold")
    split_parser.add_argument("--right", default="gate")
    split_parser.add_argument("--output-dir", type=Path, default=Path("."))
    split_parser.add_argument("--report", type=Path)

    build_parser = subparsers.add_parser("build-equiv-miter", help="Build a fresh equivalence miter between two designs")
    build_parser.add_argument("left_design", type=Path)
    build_parser.add_argument("right_design", type=Path)
    build_parser.add_argument("--left-label", default=None)
    build_parser.add_argument("--right-label", default=None)
    build_parser.add_argument(
        "--compare",
        choices=("state", "output"),
        default="state",
        help="Which shared named nodes to compare in the generated miter",
    )
    build_parser.add_argument(
        "--ignore-substring",
        action="append",
        default=[],
        help="Ignore normalized names containing this substring when matching inputs/states/outputs",
    )
    build_parser.add_argument("--output", type=Path, default=None)
    build_parser.add_argument("--report", type=Path)

    args = parser.parse_args()

    if args.command == "split-miter":
        report = split_miter(args.input, args.left, args.right, args.output_dir)
        report_path = args.report or args.output_dir / f"{args.input.stem}_split_report.json"
        _write_report(report_path, report)
        print(json.dumps({"report": str(report_path), "artifacts": report["artifacts"]}, indent=2))
        return

    if args.command == "build-equiv-miter":
        output_path = args.output or Path(
            f"{args.left_design.stem}_vs_{args.right_design.stem}_equiv.btor2"
        )
        report = build_equiv_miter(
            args.left_design,
            args.right_design,
            output_path,
            left_label=args.left_label,
            right_label=args.right_label,
            compare_op=args.compare,
            ignore_substrings=args.ignore_substring,
        )
        report_path = args.report or output_path.with_suffix(".json")
        _write_report(report_path, report)
        print(
            json.dumps(
                {
                    "report": str(report_path),
                    "output_miter": str(output_path),
                    "counts": report["counts"],
                    "btormc": report["btormc"],
                },
                indent=2,
            )
        )
        return

    raise BtorError(f"Unknown command '{args.command}'")


if __name__ == "__main__":
    main()

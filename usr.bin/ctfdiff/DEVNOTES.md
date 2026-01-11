# ctfdiff current capabilities (pre-ABI mode)

- Inputs: reads CTF from ELF (.SUNW_ctf via libelf) and falls back to treating the file as raw CTF data; optional use of .symtab/.strtab for naming global objects and functions.
- Parsing: builds an in-memory CTF type table (integer/float/array/function/struct/union/enum/typedef/qualifiers/forward/unknown) and captures member offsets as stored in CTF (bit offsets).
- Diff semantics: symmetric, name-based diff of global objects and functions; types are compared for strict structural equality (kind, encoding, size, member count/order/offset/type), with an option to ignore const qualifiers.
- Output: prints name-based diffs with "<"/">" markers; no per-type verdicts, reasons, or machine-readable output.

Gaps vs ABI-compat requirements:
- No directional BASE->NEW compatibility check or ABI-slot concept; no type selection by name or list.
- Struct/union comparison requires identical size and member order, so it cannot express "compatible with extension" semantics.
- Matching is not slot-based and does not ignore member names when assessing ABI compatibility.
- No JSON output, structured breaking-change reporting, or UNKNOWN classification.
- No explicit handling for pointer-size-driven fingerprints or recursive layout fingerprints with depth limits.

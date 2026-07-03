# Schema IR

Owns construction of protobuf-backed Schema IR from the Semantic Model and
Layout Model.

Responsibilities:

* construct backend-ready Schema IR
* preserve compiler-only metadata where appropriate
* represent resolved references using compiler-assigned object handles
* avoid semantic analysis and layout computation

Allowed dependencies:

* `compiler/semantic`
* `compiler/layout`
* `compiler/diagnostics`
* `compiler/support`
* protobuf-generated Schema IR types when generation is added

Schema IR is the only compiler representation visible to backends.

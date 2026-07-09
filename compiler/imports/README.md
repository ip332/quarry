# Imports

Owns import resolution and compilation unit construction.

Responsibilities:

* resolve source-level imports
* build the import graph
* detect missing, duplicate, ambiguous, and cyclic imports
* produce a lightweight compilation unit for later pipeline stages that need
  grouped ASTs

Allowed dependencies:

* `compiler/ast`
* `compiler/diagnostics`
* `compiler/support`

Imports do not survive as first-class objects in later compiler models.

// SPDX-License-Identifier: MPL-2.0
// Generated from the pinned C reference by scripts/typescript-builtins.py.
export const builtins = {
  "globalOptions": [
    {
      "long": "--format",
      "required": false,
      "repeatable": false,
      "summary": "Select text for humans, json for one envelope or jsonl for records.",
      "argument": {
        "name": "VALUE",
        "type": "choice",
        "choices": [
          "text",
          "json",
          "jsonl"
        ]
      },
      "default": "text",
      "requires": [],
      "conflictsWith": []
    },
    {
      "long": "--json",
      "required": false,
      "repeatable": false,
      "summary": "Exact alias of --format json.",
      "requires": [],
      "conflictsWith": []
    },
    {
      "long": "--compact",
      "required": false,
      "repeatable": false,
      "summary": "Render JSON on a single line.",
      "requires": [],
      "conflictsWith": []
    },
    {
      "long": "--pretty",
      "required": false,
      "repeatable": false,
      "summary": "--pretty=false selects compact JSON.",
      "requires": [],
      "conflictsWith": []
    },
    {
      "long": "--non-interactive",
      "required": false,
      "repeatable": false,
      "summary": "Never prompt; fail instead of asking a question.",
      "requires": [],
      "conflictsWith": []
    },
    {
      "long": "--color",
      "required": false,
      "repeatable": false,
      "summary": "Control ANSI colors on terminals.",
      "argument": {
        "name": "VALUE",
        "type": "choice",
        "choices": [
          "auto",
          "always",
          "never"
        ]
      },
      "default": "auto",
      "requires": [],
      "conflictsWith": []
    },
    {
      "long": "--progress",
      "required": false,
      "repeatable": false,
      "summary": "Show the progress of a long run on stderr in text mode: auto only when stderr is a terminal.",
      "argument": {
        "name": "VALUE",
        "type": "choice",
        "choices": [
          "auto",
          "always",
          "never"
        ]
      },
      "default": "auto",
      "requires": [],
      "conflictsWith": []
    },
    {
      "long": "--verbose",
      "required": false,
      "repeatable": false,
      "summary": "Add the details of the run on stderr in text mode; silent in JSON.",
      "requires": [],
      "conflictsWith": []
    },
    {
      "long": "--pager",
      "required": false,
      "repeatable": false,
      "summary": "Page the text rendering when stdout is a terminal; never in a pipe, in JSON or under --non-interactive.",
      "argument": {
        "name": "VALUE",
        "type": "choice",
        "choices": [
          "auto",
          "always",
          "never"
        ]
      },
      "default": "auto",
      "requires": [],
      "conflictsWith": []
    },
    {
      "long": "--field",
      "required": false,
      "repeatable": false,
      "summary": "Render one top-level member of data instead of the whole result, by the text or jsonl rendering rules.",
      "argument": {
        "name": "NAME",
        "type": "string"
      },
      "requires": [],
      "conflictsWith": []
    },
    {
      "long": "--lang",
      "required": false,
      "repeatable": false,
      "summary": "Language of the human text.",
      "argument": {
        "name": "TAG",
        "type": "string",
        "pattern": "^[A-Za-z]{2,3}(-[A-Za-z]{4})?(-([A-Za-z]{2}|[0-9]{3}))?$"
      },
      "requires": [],
      "conflictsWith": []
    },
    {
      "long": "--help",
      "required": false,
      "repeatable": false,
      "summary": "Show the help of the selected command.",
      "requires": [],
      "conflictsWith": []
    }
  ],
  "invariants": [
    "usage and agent discovery share one catalog",
    "transactional commands plan by default and require --apply",
    "stdout carries success data; stderr carries failures",
    "--json and --format json are identical",
    "unknown or duplicated options are refused",
    "stream commands reject envelope rendering flags",
    "external commands receive their arguments verbatim"
  ],
  "output": {
    "contract": "agent-cli/v2",
    "schemaVersion": 2,
    "stdout": "success data only; protocol streams are explicit exceptions",
    "stderr": "diagnostics and failure envelopes"
  },
  "commands": [
    {
      "id": "help",
      "pattern": [
        "help"
      ],
      "usage": "help [COMMAND_ID] | --help",
      "purpose": "Show the help of the program, of one command or of one family.",
      "effect": "read",
      "outputMode": "json-envelope",
      "external": false,
      "hidden": false,
      "available": true,
      "input": {
        "synopsis": "help [COMMAND_ID] | --help",
        "operands": [
          {
            "name": "COMMAND_ID",
            "required": false,
            "variadic": false,
            "summary": "Stable command identifier whose help is shown, or the family of commands under an identifier."
          }
        ],
        "options": [],
        "constraints": [],
        "passthrough": false
      },
      "outputSchema": {
        "type": "object",
        "additionalProperties": false,
        "required": [
          "text",
          "commands"
        ],
        "properties": {
          "text": {
            "type": "string"
          },
          "commands": {
            "type": "array",
            "items": {
              "type": "string"
            }
          }
        }
      },
      "exitCodes": {
        "0": "command completed",
        "1": "execution failed",
        "2": "valid report with violations"
      }
    },
    {
      "id": "version",
      "pattern": [
        "version"
      ],
      "usage": "version | --version",
      "purpose": "Show the product, program, version and contract.",
      "effect": "read",
      "outputMode": "json-envelope",
      "external": false,
      "hidden": false,
      "available": true,
      "input": {
        "synopsis": "version | --version",
        "operands": [],
        "options": [],
        "constraints": [],
        "passthrough": false
      },
      "outputSchema": {
        "type": "object",
        "additionalProperties": false,
        "required": [
          "product",
          "program",
          "version",
          "contract",
          "cliApi",
          "framework",
          "lang"
        ],
        "properties": {
          "product": {
            "type": "string"
          },
          "program": {
            "type": "string"
          },
          "version": {
            "type": "string"
          },
          "contract": {
            "const": "agent-cli/v2"
          },
          "cliApi": {
            "type": "integer"
          },
          "framework": {
            "type": "string"
          },
          "lang": {
            "type": "string"
          }
        }
      },
      "exitCodes": {
        "0": "command completed",
        "1": "execution failed",
        "2": "valid report with violations"
      }
    },
    {
      "id": "describe",
      "pattern": [
        "describe"
      ],
      "usage": "describe [COMMAND_ID] [--summary] [--prefix PREFIX]",
      "purpose": "Show the machine-readable catalog, its summary or one descriptor.",
      "effect": "read",
      "outputMode": "json-envelope",
      "external": false,
      "hidden": false,
      "available": true,
      "input": {
        "synopsis": "describe [COMMAND_ID] [--summary] [--prefix PREFIX]",
        "operands": [
          {
            "name": "COMMAND_ID",
            "required": false,
            "variadic": false,
            "summary": "Stable command identifier returned by the catalog."
          }
        ],
        "options": [
          {
            "long": "--summary",
            "required": false,
            "repeatable": false,
            "summary": "Return the compact command inventory without output schemas.",
            "requires": [],
            "conflictsWith": []
          },
          {
            "long": "--prefix",
            "required": false,
            "repeatable": false,
            "summary": "Restrict the summary to one command namespace: the command named PREFIX and every command whose identifier starts with PREFIX followed by a dot.",
            "argument": {
              "name": "PREFIX",
              "type": "string",
              "pattern": "^[a-z]([a-z0-9.-]*[a-z0-9-])?$"
            },
            "requires": [
              "--summary"
            ],
            "conflictsWith": [
              "COMMAND_ID"
            ]
          }
        ],
        "constraints": [
          {
            "kind": "requires",
            "options": [
              "--prefix",
              "--summary"
            ]
          }
        ],
        "passthrough": false
      },
      "outputSchema": {
        "type": "object",
        "description": "Command catalog, summary or single descriptor.",
        "additionalProperties": true,
        "required": [
          "schemaVersion",
          "kind",
          "program",
          "commands"
        ]
      },
      "exitCodes": {
        "0": "command completed",
        "1": "execution failed",
        "2": "valid report with violations"
      }
    },
    {
      "id": "completion",
      "pattern": [
        "completion"
      ],
      "usage": "completion SHELL",
      "purpose": "Print the shell completion script generated from the catalog.",
      "effect": "read",
      "outputMode": "json-envelope",
      "external": false,
      "hidden": false,
      "available": true,
      "input": {
        "synopsis": "completion SHELL",
        "operands": [
          {
            "name": "SHELL",
            "required": true,
            "variadic": false,
            "summary": "Shell whose completion script is printed.",
            "type": "choice",
            "choices": [
              "bash",
              "zsh",
              "fish"
            ]
          }
        ],
        "options": [],
        "constraints": [],
        "passthrough": false
      },
      "outputSchema": {
        "type": "object",
        "additionalProperties": false,
        "required": [
          "shell",
          "script"
        ],
        "properties": {
          "shell": {
            "enum": [
              "bash",
              "zsh",
              "fish"
            ]
          },
          "script": {
            "type": "string"
          }
        }
      },
      "exitCodes": {
        "0": "command completed",
        "1": "execution failed",
        "2": "valid report with violations"
      }
    },
    {
      "id": "complete.candidates",
      "pattern": [
        "__complete"
      ],
      "usage": "__complete [WORDS...]",
      "purpose": "Return completion candidates for a partial command line.",
      "effect": "read",
      "outputMode": "json-records",
      "external": false,
      "hidden": true,
      "available": true,
      "input": {
        "synopsis": "__complete [WORDS...]",
        "operands": [
          {
            "name": "WORDS",
            "required": false,
            "variadic": true,
            "summary": "Command line words after the program name; the last one is the prefix being completed."
          }
        ],
        "options": [],
        "constraints": [],
        "passthrough": false
      },
      "outputSchema": {
        "type": "object",
        "additionalProperties": false,
        "required": [
          "count",
          "records"
        ],
        "properties": {
          "count": {
            "type": "integer"
          },
          "records": {
            "type": "array",
            "items": {
              "type": "object",
              "required": [
                "word"
              ]
            }
          }
        }
      },
      "exitCodes": {
        "0": "command completed",
        "1": "execution failed",
        "2": "valid report with violations"
      }
    }
  ]
} as const;

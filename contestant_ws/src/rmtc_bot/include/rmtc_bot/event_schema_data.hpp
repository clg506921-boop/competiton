#pragma once
namespace rmtc {
inline constexpr const char* kEventSchema = R"RMTC_SCHEMA({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "RMTC normalized event interpretation contract v3",
  "description": "Validate AFTER normalization (wrapper removal, unknown-key removal, missing-nullable completion). One source per request. forecast needs start_tick/area/trigger/params of its type; activated needs either related_event_id (inherit the rest) or the full forecast fields; granted/ended need related_event_id from the request's known_events list; granted also needs subject_ids; at_tick may be null (unknown occurrence time stays null; record observation tick separately, never as occurrence time); report is informational only (hit/trigger reports) and never activates or changes effects. Derivable ticks (end_tick, trigger_end_tick, effect_end_tick) may be null; runtime derives them from start_tick+params and rejects contradictions. Runtime also checks map bounds, rect min<=max, odd attack_size/size, source ids and known event ids. Unknown events are stored only. Public event/window end and individual effect end are separate; apply type-specific lifetime rules. Structural acceptance alone does not validate chronology or authorize an effect mutation.",
  "type": "object",
  "properties": {
    "schema_version": {
      "const": 3
    },
    "events": {
      "type": "array",
      "maxItems": 4,
      "items": {
        "type": "object",
        "properties": {
          "source_ids": {
            "type": "array",
            "items": {
              "type": "string",
              "minLength": 1
            },
            "minItems": 1,
            "maxItems": 1,
            "uniqueItems": true
          },
          "type": {
            "enum": [
              "explosion",
              "illusion",
              "angel",
              "unrivaled",
              "world_split",
              "chili",
              "reflection",
              "vulnerability",
              "unknown"
            ]
          },
          "phase": {
            "enum": [
              "forecast",
              "activated",
              "granted",
              "ended",
              "report"
            ]
          },
          "announce_tick": {
            "anyOf": [
              {
                "type": "integer",
                "minimum": 0,
                "maximum": 9223372036854775807
              },
              {
                "type": "null"
              }
            ]
          },
          "start_tick": {
            "anyOf": [
              {
                "type": "integer",
                "minimum": 0,
                "maximum": 9223372036854775807
              },
              {
                "type": "null"
              }
            ]
          },
          "at_tick": {
            "anyOf": [
              {
                "type": "integer",
                "minimum": 0,
                "maximum": 9223372036854775807
              },
              {
                "type": "null"
              }
            ]
          },
          "end_tick": {
            "anyOf": [
              {
                "type": "integer",
                "minimum": 0,
                "maximum": 9223372036854775807
              },
              {
                "type": "null"
              }
            ]
          },
          "trigger_end_tick": {
            "anyOf": [
              {
                "type": "integer",
                "minimum": 0,
                "maximum": 9223372036854775807
              },
              {
                "type": "null"
              }
            ]
          },
          "effect_end_tick": {
            "anyOf": [
              {
                "type": "integer",
                "minimum": 0,
                "maximum": 9223372036854775807
              },
              {
                "type": "null"
              }
            ]
          },
          "area": {
            "anyOf": [
              {
                "oneOf": [
                  {
                    "type": "object",
                    "properties": {
                      "kind": {
                        "const": "rect"
                      },
                      "min": {
                        "type": "array",
                        "items": {
                          "type": "integer",
                          "minimum": 0
                        },
                        "minItems": 2,
                        "maxItems": 2
                      },
                      "max": {
                        "type": "array",
                        "items": {
                          "type": "integer",
                          "minimum": 0
                        },
                        "minItems": 2,
                        "maxItems": 2
                      }
                    },
                    "required": [
                      "kind",
                      "min",
                      "max"
                    ],
                    "additionalProperties": false
                  },
                  {
                    "type": "object",
                    "properties": {
                      "kind": {
                        "const": "points"
                      },
                      "at": {
                        "type": "array",
                        "items": {
                          "type": "array",
                          "items": {
                            "type": "integer",
                            "minimum": 0
                          },
                          "minItems": 2,
                          "maxItems": 2
                        },
                        "minItems": 1,
                        "maxItems": 16,
                        "uniqueItems": true
                      }
                    },
                    "required": [
                      "kind",
                      "at"
                    ],
                    "additionalProperties": false
                  },
                  {
                    "type": "object",
                    "properties": {
                      "kind": {
                        "const": "square"
                      },
                      "center": {
                        "type": "array",
                        "items": {
                          "type": "integer",
                          "minimum": 0
                        },
                        "minItems": 2,
                        "maxItems": 2
                      },
                      "size": {
                        "type": "integer",
                        "minimum": 1,
                        "maximum": 2147483647
                      }
                    },
                    "required": [
                      "kind",
                      "center",
                      "size"
                    ],
                    "additionalProperties": false
                  },
                  {
                    "type": "object",
                    "properties": {
                      "kind": {
                        "const": "cross"
                      },
                      "at": {
                        "type": "array",
                        "items": {
                          "type": "integer",
                          "minimum": 0
                        },
                        "minItems": 2,
                        "maxItems": 2
                      }
                    },
                    "required": [
                      "kind",
                      "at"
                    ],
                    "additionalProperties": false
                  },
                  {
                    "type": "object",
                    "properties": {
                      "kind": {
                        "const": "global"
                      }
                    },
                    "required": [
                      "kind"
                    ],
                    "additionalProperties": false
                  }
                ]
              },
              {
                "type": "null"
              }
            ]
          },
          "trigger": {
            "enum": [
              "activation",
              "entry",
              "movement",
              "dwell",
              "attack_defender",
              null
            ]
          },
          "subject_ids": {
            "anyOf": [
              {
                "type": "array",
                "items": {
                  "enum": [
                    "A",
                    "B",
                    "C",
                    "D"
                  ]
                },
                "minItems": 1,
                "maxItems": 4,
                "uniqueItems": true
              },
              {
                "type": "null"
              }
            ]
          },
          "related_event_id": {
            "anyOf": [
              {
                "type": "string",
                "minLength": 1
              },
              {
                "type": "null"
              }
            ]
          },
          "params": {
            "type": [
              "object",
              "null"
            ]
          }
        },
        "required": [
          "source_ids",
          "type",
          "phase",
          "announce_tick",
          "start_tick",
          "at_tick",
          "end_tick",
          "trigger_end_tick",
          "effect_end_tick",
          "area",
          "trigger",
          "subject_ids",
          "related_event_id",
          "params"
        ],
        "additionalProperties": false,
        "allOf": [
          {
            "if": {
              "properties": {
                "type": {
                  "const": "explosion"
                }
              },
              "required": [
                "type"
              ]
            },
            "then": {
              "properties": {
                "area": {
                  "anyOf": [
                    {
                      "type": "object",
                      "properties": {
                        "kind": {
                          "const": "rect"
                        },
                        "min": {
                          "type": "array",
                          "items": {
                            "type": "integer",
                            "minimum": 0
                          },
                          "minItems": 2,
                          "maxItems": 2
                        },
                        "max": {
                          "type": "array",
                          "items": {
                            "type": "integer",
                            "minimum": 0
                          },
                          "minItems": 2,
                          "maxItems": 2
                        }
                      },
                      "required": [
                        "kind",
                        "min",
                        "max"
                      ],
                      "additionalProperties": false
                    },
                    {
                      "type": "null"
                    }
                  ]
                },
                "trigger": {
                  "enum": [
                    "activation",
                    null
                  ]
                },
                "params": {
                  "anyOf": [
                    {
                      "type": "object",
                      "properties": {
                        "damage": {
                          "type": "number",
                          "minimum": 0
                        },
                        "respects_protection": {
                          "type": "boolean"
                        }
                      },
                      "required": [
                        "damage",
                        "respects_protection"
                      ],
                      "additionalProperties": false
                    },
                    {
                      "type": "null"
                    }
                  ]
                }
              }
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "explosion"
                },
                "phase": {
                  "const": "forecast"
                }
              },
              "required": [
                "type",
                "phase"
              ]
            },
            "then": {
              "properties": {
                "start_tick": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 9223372036854775807
                },
                "area": {
                  "type": "object",
                  "properties": {
                    "kind": {
                      "const": "rect"
                    },
                    "min": {
                      "type": "array",
                      "items": {
                        "type": "integer",
                        "minimum": 0
                      },
                      "minItems": 2,
                      "maxItems": 2
                    },
                    "max": {
                      "type": "array",
                      "items": {
                        "type": "integer",
                        "minimum": 0
                      },
                      "minItems": 2,
                      "maxItems": 2
                    }
                  },
                  "required": [
                    "kind",
                    "min",
                    "max"
                  ],
                  "additionalProperties": false
                },
                "trigger": {
                  "const": "activation"
                },
                "params": {
                  "type": "object",
                  "properties": {
                    "damage": {
                      "type": "number",
                      "minimum": 0
                    },
                    "respects_protection": {
                      "type": "boolean"
                    }
                  },
                  "required": [
                    "damage",
                    "respects_protection"
                  ],
                  "additionalProperties": false
                }
              }
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "explosion"
                },
                "phase": {
                  "const": "activated"
                }
              },
              "required": [
                "type",
                "phase"
              ]
            },
            "then": {
              "anyOf": [
                {
                  "properties": {
                    "related_event_id": {
                      "type": "string",
                      "minLength": 1
                    }
                  }
                },
                {
                  "properties": {
                    "start_tick": {
                      "type": "integer",
                      "minimum": 0,
                      "maximum": 9223372036854775807
                    },
                    "area": {
                      "type": "object",
                      "properties": {
                        "kind": {
                          "const": "rect"
                        },
                        "min": {
                          "type": "array",
                          "items": {
                            "type": "integer",
                            "minimum": 0
                          },
                          "minItems": 2,
                          "maxItems": 2
                        },
                        "max": {
                          "type": "array",
                          "items": {
                            "type": "integer",
                            "minimum": 0
                          },
                          "minItems": 2,
                          "maxItems": 2
                        }
                      },
                      "required": [
                        "kind",
                        "min",
                        "max"
                      ],
                      "additionalProperties": false
                    },
                    "trigger": {
                      "const": "activation"
                    },
                    "params": {
                      "type": "object",
                      "properties": {
                        "damage": {
                          "type": "number",
                          "minimum": 0
                        },
                        "respects_protection": {
                          "type": "boolean"
                        }
                      },
                      "required": [
                        "damage",
                        "respects_protection"
                      ],
                      "additionalProperties": false
                    }
                  }
                }
              ]
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "illusion"
                }
              },
              "required": [
                "type"
              ]
            },
            "then": {
              "properties": {
                "area": {
                  "anyOf": [
                    {
                      "type": "object",
                      "properties": {
                        "kind": {
                          "const": "rect"
                        },
                        "min": {
                          "type": "array",
                          "items": {
                            "type": "integer",
                            "minimum": 0
                          },
                          "minItems": 2,
                          "maxItems": 2
                        },
                        "max": {
                          "type": "array",
                          "items": {
                            "type": "integer",
                            "minimum": 0
                          },
                          "minItems": 2,
                          "maxItems": 2
                        }
                      },
                      "required": [
                        "kind",
                        "min",
                        "max"
                      ],
                      "additionalProperties": false
                    },
                    {
                      "type": "null"
                    }
                  ]
                },
                "trigger": {
                  "enum": [
                    "activation",
                    null
                  ]
                },
                "params": {
                  "anyOf": [
                    {
                      "type": "object",
                      "properties": {
                        "effect_ticks": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        },
                        "moves_only": {
                          "const": true
                        }
                      },
                      "required": [
                        "effect_ticks",
                        "moves_only"
                      ],
                      "additionalProperties": false
                    },
                    {
                      "type": "null"
                    }
                  ]
                }
              }
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "illusion"
                },
                "phase": {
                  "const": "forecast"
                }
              },
              "required": [
                "type",
                "phase"
              ]
            },
            "then": {
              "properties": {
                "start_tick": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 9223372036854775807
                },
                "area": {
                  "type": "object",
                  "properties": {
                    "kind": {
                      "const": "rect"
                    },
                    "min": {
                      "type": "array",
                      "items": {
                        "type": "integer",
                        "minimum": 0
                      },
                      "minItems": 2,
                      "maxItems": 2
                    },
                    "max": {
                      "type": "array",
                      "items": {
                        "type": "integer",
                        "minimum": 0
                      },
                      "minItems": 2,
                      "maxItems": 2
                    }
                  },
                  "required": [
                    "kind",
                    "min",
                    "max"
                  ],
                  "additionalProperties": false
                },
                "trigger": {
                  "const": "activation"
                },
                "params": {
                  "type": "object",
                  "properties": {
                    "effect_ticks": {
                      "type": "integer",
                      "minimum": 1,
                      "maximum": 2147483647
                    },
                    "moves_only": {
                      "const": true
                    }
                  },
                  "required": [
                    "effect_ticks",
                    "moves_only"
                  ],
                  "additionalProperties": false
                }
              }
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "illusion"
                },
                "phase": {
                  "const": "activated"
                }
              },
              "required": [
                "type",
                "phase"
              ]
            },
            "then": {
              "anyOf": [
                {
                  "properties": {
                    "related_event_id": {
                      "type": "string",
                      "minLength": 1
                    }
                  }
                },
                {
                  "properties": {
                    "start_tick": {
                      "type": "integer",
                      "minimum": 0,
                      "maximum": 9223372036854775807
                    },
                    "area": {
                      "type": "object",
                      "properties": {
                        "kind": {
                          "const": "rect"
                        },
                        "min": {
                          "type": "array",
                          "items": {
                            "type": "integer",
                            "minimum": 0
                          },
                          "minItems": 2,
                          "maxItems": 2
                        },
                        "max": {
                          "type": "array",
                          "items": {
                            "type": "integer",
                            "minimum": 0
                          },
                          "minItems": 2,
                          "maxItems": 2
                        }
                      },
                      "required": [
                        "kind",
                        "min",
                        "max"
                      ],
                      "additionalProperties": false
                    },
                    "trigger": {
                      "const": "activation"
                    },
                    "params": {
                      "type": "object",
                      "properties": {
                        "effect_ticks": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        },
                        "moves_only": {
                          "const": true
                        }
                      },
                      "required": [
                        "effect_ticks",
                        "moves_only"
                      ],
                      "additionalProperties": false
                    }
                  }
                }
              ]
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "angel"
                }
              },
              "required": [
                "type"
              ]
            },
            "then": {
              "properties": {
                "area": {
                  "anyOf": [
                    {
                      "type": "object",
                      "properties": {
                        "kind": {
                          "const": "points"
                        },
                        "at": {
                          "type": "array",
                          "items": {
                            "type": "array",
                            "items": {
                              "type": "integer",
                              "minimum": 0
                            },
                            "minItems": 2,
                            "maxItems": 2
                          },
                          "minItems": 1,
                          "maxItems": 16,
                          "uniqueItems": true
                        }
                      },
                      "required": [
                        "kind",
                        "at"
                      ],
                      "additionalProperties": false
                    },
                    {
                      "type": "null"
                    }
                  ]
                },
                "trigger": {
                  "enum": [
                    "entry",
                    null
                  ]
                },
                "params": {
                  "anyOf": [
                    {
                      "type": "object",
                      "properties": {
                        "duration_ticks": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        },
                        "entry_window_ticks": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        },
                        "heal_ticks": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        },
                        "heal_per_tick": {
                          "type": "number",
                          "minimum": 0
                        },
                        "hp_cap": {
                          "anyOf": [
                            {
                              "type": "number",
                              "minimum": 0
                            },
                            {
                              "type": "null"
                            }
                          ]
                        }
                      },
                      "required": [
                        "duration_ticks",
                        "entry_window_ticks",
                        "heal_ticks",
                        "heal_per_tick"
                      ],
                      "additionalProperties": false
                    },
                    {
                      "type": "null"
                    }
                  ]
                }
              }
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "angel"
                },
                "phase": {
                  "const": "forecast"
                }
              },
              "required": [
                "type",
                "phase"
              ]
            },
            "then": {
              "properties": {
                "start_tick": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 9223372036854775807
                },
                "area": {
                  "type": "object",
                  "properties": {
                    "kind": {
                      "const": "points"
                    },
                    "at": {
                      "type": "array",
                      "items": {
                        "type": "array",
                        "items": {
                          "type": "integer",
                          "minimum": 0
                        },
                        "minItems": 2,
                        "maxItems": 2
                      },
                      "minItems": 1,
                      "maxItems": 16,
                      "uniqueItems": true
                    }
                  },
                  "required": [
                    "kind",
                    "at"
                  ],
                  "additionalProperties": false
                },
                "trigger": {
                  "const": "entry"
                },
                "params": {
                  "type": "object",
                  "properties": {
                    "duration_ticks": {
                      "type": "integer",
                      "minimum": 1,
                      "maximum": 2147483647
                    },
                    "entry_window_ticks": {
                      "type": "integer",
                      "minimum": 1,
                      "maximum": 2147483647
                    },
                    "heal_ticks": {
                      "type": "integer",
                      "minimum": 1,
                      "maximum": 2147483647
                    },
                    "heal_per_tick": {
                      "type": "number",
                      "minimum": 0
                    },
                    "hp_cap": {
                      "anyOf": [
                        {
                          "type": "number",
                          "minimum": 0
                        },
                        {
                          "type": "null"
                        }
                      ]
                    }
                  },
                  "required": [
                    "duration_ticks",
                    "entry_window_ticks",
                    "heal_ticks",
                    "heal_per_tick"
                  ],
                  "additionalProperties": false
                }
              }
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "angel"
                },
                "phase": {
                  "const": "activated"
                }
              },
              "required": [
                "type",
                "phase"
              ]
            },
            "then": {
              "anyOf": [
                {
                  "properties": {
                    "related_event_id": {
                      "type": "string",
                      "minLength": 1
                    }
                  }
                },
                {
                  "properties": {
                    "start_tick": {
                      "type": "integer",
                      "minimum": 0,
                      "maximum": 9223372036854775807
                    },
                    "area": {
                      "type": "object",
                      "properties": {
                        "kind": {
                          "const": "points"
                        },
                        "at": {
                          "type": "array",
                          "items": {
                            "type": "array",
                            "items": {
                              "type": "integer",
                              "minimum": 0
                            },
                            "minItems": 2,
                            "maxItems": 2
                          },
                          "minItems": 1,
                          "maxItems": 16,
                          "uniqueItems": true
                        }
                      },
                      "required": [
                        "kind",
                        "at"
                      ],
                      "additionalProperties": false
                    },
                    "trigger": {
                      "const": "entry"
                    },
                    "params": {
                      "type": "object",
                      "properties": {
                        "duration_ticks": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        },
                        "entry_window_ticks": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        },
                        "heal_ticks": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        },
                        "heal_per_tick": {
                          "type": "number",
                          "minimum": 0
                        },
                        "hp_cap": {
                          "anyOf": [
                            {
                              "type": "number",
                              "minimum": 0
                            },
                            {
                              "type": "null"
                            }
                          ]
                        }
                      },
                      "required": [
                        "duration_ticks",
                        "entry_window_ticks",
                        "heal_ticks",
                        "heal_per_tick"
                      ],
                      "additionalProperties": false
                    }
                  }
                }
              ]
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "unrivaled"
                }
              },
              "required": [
                "type"
              ]
            },
            "then": {
              "properties": {
                "area": {
                  "anyOf": [
                    {
                      "oneOf": [
                        {
                          "type": "object",
                          "properties": {
                            "kind": {
                              "const": "square"
                            },
                            "center": {
                              "type": "array",
                              "items": {
                                "type": "integer",
                                "minimum": 0
                              },
                              "minItems": 2,
                              "maxItems": 2
                            },
                            "size": {
                              "type": "integer",
                              "minimum": 1,
                              "maximum": 2147483647
                            }
                          },
                          "required": [
                            "kind",
                            "center",
                            "size"
                          ],
                          "additionalProperties": false
                        },
                        {
                          "type": "object",
                          "properties": {
                            "kind": {
                              "const": "rect"
                            },
                            "min": {
                              "type": "array",
                              "items": {
                                "type": "integer",
                                "minimum": 0
                              },
                              "minItems": 2,
                              "maxItems": 2
                            },
                            "max": {
                              "type": "array",
                              "items": {
                                "type": "integer",
                                "minimum": 0
                              },
                              "minItems": 2,
                              "maxItems": 2
                            }
                          },
                          "required": [
                            "kind",
                            "min",
                            "max"
                          ],
                          "additionalProperties": false
                        }
                      ]
                    },
                    {
                      "type": "null"
                    }
                  ]
                },
                "trigger": {
                  "enum": [
                    "dwell",
                    null
                  ]
                },
                "params": {
                  "anyOf": [
                    {
                      "type": "object",
                      "properties": {
                        "duration_ticks": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        },
                        "required_dwell_ticks": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        },
                        "attack_size": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        },
                        "damage": {
                          "type": "number",
                          "minimum": 0
                        },
                        "defended_damage": {
                          "type": "number",
                          "minimum": 0
                        }
                      },
                      "required": [
                        "duration_ticks",
                        "required_dwell_ticks",
                        "attack_size",
                        "damage",
                        "defended_damage"
                      ],
                      "additionalProperties": false
                    },
                    {
                      "type": "null"
                    }
                  ]
                }
              }
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "unrivaled"
                },
                "phase": {
                  "const": "forecast"
                }
              },
              "required": [
                "type",
                "phase"
              ]
            },
            "then": {
              "properties": {
                "start_tick": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 9223372036854775807
                },
                "area": {
                  "oneOf": [
                    {
                      "type": "object",
                      "properties": {
                        "kind": {
                          "const": "square"
                        },
                        "center": {
                          "type": "array",
                          "items": {
                            "type": "integer",
                            "minimum": 0
                          },
                          "minItems": 2,
                          "maxItems": 2
                        },
                        "size": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        }
                      },
                      "required": [
                        "kind",
                        "center",
                        "size"
                      ],
                      "additionalProperties": false
                    },
                    {
                      "type": "object",
                      "properties": {
                        "kind": {
                          "const": "rect"
                        },
                        "min": {
                          "type": "array",
                          "items": {
                            "type": "integer",
                            "minimum": 0
                          },
                          "minItems": 2,
                          "maxItems": 2
                        },
                        "max": {
                          "type": "array",
                          "items": {
                            "type": "integer",
                            "minimum": 0
                          },
                          "minItems": 2,
                          "maxItems": 2
                        }
                      },
                      "required": [
                        "kind",
                        "min",
                        "max"
                      ],
                      "additionalProperties": false
                    }
                  ]
                },
                "trigger": {
                  "const": "dwell"
                },
                "params": {
                  "type": "object",
                  "properties": {
                    "duration_ticks": {
                      "type": "integer",
                      "minimum": 1,
                      "maximum": 2147483647
                    },
                    "required_dwell_ticks": {
                      "type": "integer",
                      "minimum": 1,
                      "maximum": 2147483647
                    },
                    "attack_size": {
                      "type": "integer",
                      "minimum": 1,
                      "maximum": 2147483647
                    },
                    "damage": {
                      "type": "number",
                      "minimum": 0
                    },
                    "defended_damage": {
                      "type": "number",
                      "minimum": 0
                    }
                  },
                  "required": [
                    "duration_ticks",
                    "required_dwell_ticks",
                    "attack_size",
                    "damage",
                    "defended_damage"
                  ],
                  "additionalProperties": false
                }
              }
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "unrivaled"
                },
                "phase": {
                  "const": "activated"
                }
              },
              "required": [
                "type",
                "phase"
              ]
            },
            "then": {
              "anyOf": [
                {
                  "properties": {
                    "related_event_id": {
                      "type": "string",
                      "minLength": 1
                    }
                  }
                },
                {
                  "properties": {
                    "start_tick": {
                      "type": "integer",
                      "minimum": 0,
                      "maximum": 9223372036854775807
                    },
                    "area": {
                      "oneOf": [
                        {
                          "type": "object",
                          "properties": {
                            "kind": {
                              "const": "square"
                            },
                            "center": {
                              "type": "array",
                              "items": {
                                "type": "integer",
                                "minimum": 0
                              },
                              "minItems": 2,
                              "maxItems": 2
                            },
                            "size": {
                              "type": "integer",
                              "minimum": 1,
                              "maximum": 2147483647
                            }
                          },
                          "required": [
                            "kind",
                            "center",
                            "size"
                          ],
                          "additionalProperties": false
                        },
                        {
                          "type": "object",
                          "properties": {
                            "kind": {
                              "const": "rect"
                            },
                            "min": {
                              "type": "array",
                              "items": {
                                "type": "integer",
                                "minimum": 0
                              },
                              "minItems": 2,
                              "maxItems": 2
                            },
                            "max": {
                              "type": "array",
                              "items": {
                                "type": "integer",
                                "minimum": 0
                              },
                              "minItems": 2,
                              "maxItems": 2
                            }
                          },
                          "required": [
                            "kind",
                            "min",
                            "max"
                          ],
                          "additionalProperties": false
                        }
                      ]
                    },
                    "trigger": {
                      "const": "dwell"
                    },
                    "params": {
                      "type": "object",
                      "properties": {
                        "duration_ticks": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        },
                        "required_dwell_ticks": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        },
                        "attack_size": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        },
                        "damage": {
                          "type": "number",
                          "minimum": 0
                        },
                        "defended_damage": {
                          "type": "number",
                          "minimum": 0
                        }
                      },
                      "required": [
                        "duration_ticks",
                        "required_dwell_ticks",
                        "attack_size",
                        "damage",
                        "defended_damage"
                      ],
                      "additionalProperties": false
                    }
                  }
                }
              ]
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "world_split"
                }
              },
              "required": [
                "type"
              ]
            },
            "then": {
              "properties": {
                "area": {
                  "anyOf": [
                    {
                      "type": "object",
                      "properties": {
                        "kind": {
                          "const": "cross"
                        },
                        "at": {
                          "type": "array",
                          "items": {
                            "type": "integer",
                            "minimum": 0
                          },
                          "minItems": 2,
                          "maxItems": 2
                        }
                      },
                      "required": [
                        "kind",
                        "at"
                      ],
                      "additionalProperties": false
                    },
                    {
                      "type": "null"
                    }
                  ]
                },
                "trigger": {
                  "enum": [
                    "activation",
                    null
                  ]
                },
                "params": {
                  "anyOf": [
                    {
                      "type": "object",
                      "properties": {
                        "duration_ticks": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        }
                      },
                      "required": [
                        "duration_ticks"
                      ],
                      "additionalProperties": false
                    },
                    {
                      "type": "null"
                    }
                  ]
                }
              }
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "world_split"
                },
                "phase": {
                  "const": "forecast"
                }
              },
              "required": [
                "type",
                "phase"
              ]
            },
            "then": {
              "properties": {
                "start_tick": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 9223372036854775807
                },
                "area": {
                  "type": "object",
                  "properties": {
                    "kind": {
                      "const": "cross"
                    },
                    "at": {
                      "type": "array",
                      "items": {
                        "type": "integer",
                        "minimum": 0
                      },
                      "minItems": 2,
                      "maxItems": 2
                    }
                  },
                  "required": [
                    "kind",
                    "at"
                  ],
                  "additionalProperties": false
                },
                "trigger": {
                  "const": "activation"
                },
                "params": {
                  "type": "object",
                  "properties": {
                    "duration_ticks": {
                      "type": "integer",
                      "minimum": 1,
                      "maximum": 2147483647
                    }
                  },
                  "required": [
                    "duration_ticks"
                  ],
                  "additionalProperties": false
                }
              }
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "world_split"
                },
                "phase": {
                  "const": "activated"
                }
              },
              "required": [
                "type",
                "phase"
              ]
            },
            "then": {
              "anyOf": [
                {
                  "properties": {
                    "related_event_id": {
                      "type": "string",
                      "minLength": 1
                    }
                  }
                },
                {
                  "properties": {
                    "start_tick": {
                      "type": "integer",
                      "minimum": 0,
                      "maximum": 9223372036854775807
                    },
                    "area": {
                      "type": "object",
                      "properties": {
                        "kind": {
                          "const": "cross"
                        },
                        "at": {
                          "type": "array",
                          "items": {
                            "type": "integer",
                            "minimum": 0
                          },
                          "minItems": 2,
                          "maxItems": 2
                        }
                      },
                      "required": [
                        "kind",
                        "at"
                      ],
                      "additionalProperties": false
                    },
                    "trigger": {
                      "const": "activation"
                    },
                    "params": {
                      "type": "object",
                      "properties": {
                        "duration_ticks": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        }
                      },
                      "required": [
                        "duration_ticks"
                      ],
                      "additionalProperties": false
                    }
                  }
                }
              ]
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "chili"
                }
              },
              "required": [
                "type"
              ]
            },
            "then": {
              "properties": {
                "area": {
                  "anyOf": [
                    {
                      "type": "object",
                      "properties": {
                        "kind": {
                          "const": "points"
                        },
                        "at": {
                          "type": "array",
                          "items": {
                            "type": "array",
                            "items": {
                              "type": "integer",
                              "minimum": 0
                            },
                            "minItems": 2,
                            "maxItems": 2
                          },
                          "minItems": 1,
                          "maxItems": 16,
                          "uniqueItems": true
                        }
                      },
                      "required": [
                        "kind",
                        "at"
                      ],
                      "additionalProperties": false
                    },
                    {
                      "type": "null"
                    }
                  ]
                },
                "trigger": {
                  "enum": [
                    "entry",
                    null
                  ]
                },
                "params": {
                  "anyOf": [
                    {
                      "type": "object",
                      "properties": {
                        "duration_ticks": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        },
                        "damage": {
                          "type": "number",
                          "minimum": 0
                        },
                        "respects_protection": {
                          "type": "boolean"
                        }
                      },
                      "required": [
                        "duration_ticks",
                        "damage",
                        "respects_protection"
                      ],
                      "additionalProperties": false
                    },
                    {
                      "type": "null"
                    }
                  ]
                }
              }
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "chili"
                },
                "phase": {
                  "const": "forecast"
                }
              },
              "required": [
                "type",
                "phase"
              ]
            },
            "then": {
              "properties": {
                "start_tick": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 9223372036854775807
                },
                "area": {
                  "type": "object",
                  "properties": {
                    "kind": {
                      "const": "points"
                    },
                    "at": {
                      "type": "array",
                      "items": {
                        "type": "array",
                        "items": {
                          "type": "integer",
                          "minimum": 0
                        },
                        "minItems": 2,
                        "maxItems": 2
                      },
                      "minItems": 1,
                      "maxItems": 16,
                      "uniqueItems": true
                    }
                  },
                  "required": [
                    "kind",
                    "at"
                  ],
                  "additionalProperties": false
                },
                "trigger": {
                  "const": "entry"
                },
                "params": {
                  "type": "object",
                  "properties": {
                    "duration_ticks": {
                      "type": "integer",
                      "minimum": 1,
                      "maximum": 2147483647
                    },
                    "damage": {
                      "type": "number",
                      "minimum": 0
                    },
                    "respects_protection": {
                      "type": "boolean"
                    }
                  },
                  "required": [
                    "duration_ticks",
                    "damage",
                    "respects_protection"
                  ],
                  "additionalProperties": false
                }
              }
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "chili"
                },
                "phase": {
                  "const": "activated"
                }
              },
              "required": [
                "type",
                "phase"
              ]
            },
            "then": {
              "anyOf": [
                {
                  "properties": {
                    "related_event_id": {
                      "type": "string",
                      "minLength": 1
                    }
                  }
                },
                {
                  "properties": {
                    "start_tick": {
                      "type": "integer",
                      "minimum": 0,
                      "maximum": 9223372036854775807
                    },
                    "area": {
                      "type": "object",
                      "properties": {
                        "kind": {
                          "const": "points"
                        },
                        "at": {
                          "type": "array",
                          "items": {
                            "type": "array",
                            "items": {
                              "type": "integer",
                              "minimum": 0
                            },
                            "minItems": 2,
                            "maxItems": 2
                          },
                          "minItems": 1,
                          "maxItems": 16,
                          "uniqueItems": true
                        }
                      },
                      "required": [
                        "kind",
                        "at"
                      ],
                      "additionalProperties": false
                    },
                    "trigger": {
                      "const": "entry"
                    },
                    "params": {
                      "type": "object",
                      "properties": {
                        "duration_ticks": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        },
                        "damage": {
                          "type": "number",
                          "minimum": 0
                        },
                        "respects_protection": {
                          "type": "boolean"
                        }
                      },
                      "required": [
                        "duration_ticks",
                        "damage",
                        "respects_protection"
                      ],
                      "additionalProperties": false
                    }
                  }
                }
              ]
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "reflection"
                }
              },
              "required": [
                "type"
              ]
            },
            "then": {
              "properties": {
                "area": {
                  "anyOf": [
                    {
                      "type": "object",
                      "properties": {
                        "kind": {
                          "const": "global"
                        }
                      },
                      "required": [
                        "kind"
                      ],
                      "additionalProperties": false
                    },
                    {
                      "type": "null"
                    }
                  ]
                },
                "trigger": {
                  "enum": [
                    "attack_defender",
                    null
                  ]
                },
                "params": {
                  "anyOf": [
                    {
                      "type": "object",
                      "properties": {
                        "duration_ticks": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        },
                        "reflect_damage": {
                          "type": "number",
                          "minimum": 0
                        }
                      },
                      "required": [
                        "duration_ticks",
                        "reflect_damage"
                      ],
                      "additionalProperties": false
                    },
                    {
                      "type": "null"
                    }
                  ]
                }
              }
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "reflection"
                },
                "phase": {
                  "const": "forecast"
                }
              },
              "required": [
                "type",
                "phase"
              ]
            },
            "then": {
              "properties": {
                "start_tick": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 9223372036854775807
                },
                "area": {
                  "type": "object",
                  "properties": {
                    "kind": {
                      "const": "global"
                    }
                  },
                  "required": [
                    "kind"
                  ],
                  "additionalProperties": false
                },
                "trigger": {
                  "const": "attack_defender"
                },
                "params": {
                  "type": "object",
                  "properties": {
                    "duration_ticks": {
                      "type": "integer",
                      "minimum": 1,
                      "maximum": 2147483647
                    },
                    "reflect_damage": {
                      "type": "number",
                      "minimum": 0
                    }
                  },
                  "required": [
                    "duration_ticks",
                    "reflect_damage"
                  ],
                  "additionalProperties": false
                }
              }
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "reflection"
                },
                "phase": {
                  "const": "activated"
                }
              },
              "required": [
                "type",
                "phase"
              ]
            },
            "then": {
              "anyOf": [
                {
                  "properties": {
                    "related_event_id": {
                      "type": "string",
                      "minLength": 1
                    }
                  }
                },
                {
                  "properties": {
                    "start_tick": {
                      "type": "integer",
                      "minimum": 0,
                      "maximum": 9223372036854775807
                    },
                    "area": {
                      "type": "object",
                      "properties": {
                        "kind": {
                          "const": "global"
                        }
                      },
                      "required": [
                        "kind"
                      ],
                      "additionalProperties": false
                    },
                    "trigger": {
                      "const": "attack_defender"
                    },
                    "params": {
                      "type": "object",
                      "properties": {
                        "duration_ticks": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        },
                        "reflect_damage": {
                          "type": "number",
                          "minimum": 0
                        }
                      },
                      "required": [
                        "duration_ticks",
                        "reflect_damage"
                      ],
                      "additionalProperties": false
                    }
                  }
                }
              ]
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "vulnerability"
                }
              },
              "required": [
                "type"
              ]
            },
            "then": {
              "properties": {
                "area": {
                  "anyOf": [
                    {
                      "type": "object",
                      "properties": {
                        "kind": {
                          "const": "global"
                        }
                      },
                      "required": [
                        "kind"
                      ],
                      "additionalProperties": false
                    },
                    {
                      "type": "null"
                    }
                  ]
                },
                "trigger": {
                  "enum": [
                    "movement",
                    null
                  ]
                },
                "params": {
                  "anyOf": [
                    {
                      "type": "object",
                      "properties": {
                        "movement_window_ticks": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        },
                        "multiplier": {
                          "type": "number",
                          "minimum": 1
                        },
                        "vulnerability_ticks": {
                          "anyOf": [
                            {
                              "type": "integer",
                              "minimum": 1,
                              "maximum": 2147483647
                            },
                            {
                              "type": "null"
                            }
                          ]
                        }
                      },
                      "required": [
                        "movement_window_ticks",
                        "multiplier"
                      ],
                      "additionalProperties": false
                    },
                    {
                      "type": "null"
                    }
                  ]
                }
              }
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "vulnerability"
                },
                "phase": {
                  "const": "forecast"
                }
              },
              "required": [
                "type",
                "phase"
              ]
            },
            "then": {
              "allOf": [
                {
                  "properties": {
                    "start_tick": {
                      "type": "integer",
                      "minimum": 0,
                      "maximum": 9223372036854775807
                    },
                    "area": {
                      "type": "object",
                      "properties": {
                        "kind": {
                          "const": "global"
                        }
                      },
                      "required": [
                        "kind"
                      ],
                      "additionalProperties": false
                    },
                    "trigger": {
                      "const": "movement"
                    },
                    "params": {
                      "type": "object",
                      "properties": {
                        "movement_window_ticks": {
                          "type": "integer",
                          "minimum": 1,
                          "maximum": 2147483647
                        },
                        "multiplier": {
                          "type": "number",
                          "minimum": 1
                        },
                        "vulnerability_ticks": {
                          "anyOf": [
                            {
                              "type": "integer",
                              "minimum": 1,
                              "maximum": 2147483647
                            },
                            {
                              "type": "null"
                            }
                          ]
                        }
                      },
                      "required": [
                        "movement_window_ticks",
                        "multiplier"
                      ],
                      "additionalProperties": false
                    }
                  }
                },
                {
                  "anyOf": [
                    {
                      "properties": {
                        "effect_end_tick": {
                          "type": "integer",
                          "minimum": 0,
                          "maximum": 9223372036854775807
                        }
                      }
                    },
                    {
                      "properties": {
                        "params": {
                          "properties": {
                            "vulnerability_ticks": {
                              "type": "integer",
                              "minimum": 1,
                              "maximum": 2147483647
                            }
                          },
                          "required": [
                            "vulnerability_ticks"
                          ]
                        }
                      }
                    }
                  ]
                }
              ]
            }
          },
          {
            "if": {
              "properties": {
                "type": {
                  "const": "vulnerability"
                },
                "phase": {
                  "const": "activated"
                }
              },
              "required": [
                "type",
                "phase"
              ]
            },
            "then": {
              "anyOf": [
                {
                  "properties": {
                    "related_event_id": {
                      "type": "string",
                      "minLength": 1
                    }
                  }
                },
                {
                  "allOf": [
                    {
                      "properties": {
                        "start_tick": {
                          "type": "integer",
                          "minimum": 0,
                          "maximum": 9223372036854775807
                        },
                        "area": {
                          "type": "object",
                          "properties": {
                            "kind": {
                              "const": "global"
                            }
                          },
                          "required": [
                            "kind"
                          ],
                          "additionalProperties": false
                        },
                        "trigger": {
                          "const": "movement"
                        },
                        "params": {
                          "type": "object",
                          "properties": {
                            "movement_window_ticks": {
                              "type": "integer",
                              "minimum": 1,
                              "maximum": 2147483647
                            },
                            "multiplier": {
                              "type": "number",
                              "minimum": 1
                            },
                            "vulnerability_ticks": {
                              "anyOf": [
                                {
                                  "type": "integer",
                                  "minimum": 1,
                                  "maximum": 2147483647
                                },
                                {
                                  "type": "null"
                                }
                              ]
                            }
                          },
                          "required": [
                            "movement_window_ticks",
                            "multiplier"
                          ],
                          "additionalProperties": false
                        }
                      }
                    },
                    {
                      "anyOf": [
                        {
                          "properties": {
                            "effect_end_tick": {
                              "type": "integer",
                              "minimum": 0,
                              "maximum": 9223372036854775807
                            }
                          }
                        },
                        {
                          "properties": {
                            "params": {
                              "properties": {
                                "vulnerability_ticks": {
                                  "type": "integer",
                                  "minimum": 1,
                                  "maximum": 2147483647
                                }
                              },
                              "required": [
                                "vulnerability_ticks"
                              ]
                            }
                          }
                        }
                      ]
                    }
                  ]
                }
              ]
            }
          },
          {
            "if": {
              "properties": {
                "phase": {
                  "const": "granted"
                }
              },
              "required": [
                "phase"
              ]
            },
            "then": {
              "properties": {
                "related_event_id": {
                  "type": "string",
                  "minLength": 1
                },
                "subject_ids": {
                  "type": "array",
                  "items": {
                    "enum": [
                      "A",
                      "B",
                      "C",
                      "D"
                    ]
                  },
                  "minItems": 1,
                  "maxItems": 4,
                  "uniqueItems": true
                }
              }
            }
          },
          {
            "if": {
              "properties": {
                "phase": {
                  "const": "ended"
                }
              },
              "required": [
                "phase"
              ]
            },
            "then": {
              "properties": {
                "related_event_id": {
                  "type": "string",
                  "minLength": 1
                }
              }
            }
          },
          {
            "if": {
              "properties": {
                "phase": {
                  "const": "report"
                }
              },
              "required": [
                "phase"
              ]
            },
            "then": {
              "properties": {
                "params": {
                  "type": "null"
                }
              }
            }
          }
        ]
      }
    },
    "unresolved_source_ids": {
      "type": "array",
      "items": {
        "type": "string",
        "minLength": 1
      },
      "maxItems": 1,
      "uniqueItems": true
    }
  },
  "required": [
    "schema_version",
    "events",
    "unresolved_source_ids"
  ],
  "additionalProperties": false
}
)RMTC_SCHEMA";
}

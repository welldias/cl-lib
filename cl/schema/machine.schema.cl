# machine.schema.cl - rules for the "machine" block used by machine.cl.
#
# Each attribute inside a `block "<type>"` declares one allowed attribute:
# just a type name, a list of allowed strings (an enum), or
# { type = "...", required = true|false, values = [...] }.
# A nested `block "<type>"` declares an allowed sub-block, with its own rules.

strict = true   # any other top-level block type is an error

block "machine" {
  cpu    = { type = "number", required = true }
  memory = { type = "string", required = true }
  tags   = "list"

  block "disk" {
    size = { type = "number", required = true }
    kind = ["ssd", "hdd", "nvme"]
  }
}

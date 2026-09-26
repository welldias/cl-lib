# "var" and "local" here are plain top-level attributes (objects), not
# Terraform's magic "variable"/"locals" blocks - that is what the cl
# library's name-agnostic resolution needs for "var.x"/"local.x" to resolve.
var = {
  first_name = "Ada"
  last_name  = "Lovelace"
  server = {
    ips = ["127.0.0.1", "192.168.0.1"]
  }
}

local = {
  full_name = concat(var.first_name, " ", var.last_name)
}

upper_name = upper(local.full_name)
ips        = concat(["0.0.0.0"], var.server.ips)

# "var" e "local" aqui sao atributos de topo literais (objetos), nao os
# blocos magicos "variable"/"locals" do Terraform - e o que a resolucao
# agnostica da lib cl exige para "var.x"/"local.x" resolverem.
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

# Este arquivo demonstra a resolucao agnostica de traversals da lib cl:
# "var" e "service" aqui sao apenas nomes literais de topo, sem nenhum
# significado especial (ao contrario do HCL/Terraform).

var = {
  name = "Cloud"
}

service "web" {
  port = 8080
}

greeting = "Hello, ${upper(var.name)}! Web runs on port ${service.web.port}"
tags     = concat(["a", "b"], ["c"])
full     = concat("foo", "-", "bar")
count    = length(tags)

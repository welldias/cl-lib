# This file shows the cl library's name-agnostic traversal resolution:
# "var" and "service" here are just plain top-level names, with no special
# meaning (unlike HCL/Terraform).

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

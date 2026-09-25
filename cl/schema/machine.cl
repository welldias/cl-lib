# machine.cl - a document that satisfies machine.schema.cl.

base_cpu = 2

machine "web" {
  cpu    = base_cpu * 2
  memory = "8GB"
  tags   = ["frontend", "public"]

  disk {
    size = 100
    kind = "ssd"
  }
}

machine "db" {
  cpu    = 8
  memory = "32GB"

  disk {
    size = 500
  }

  disk {
    size = 2000
    kind = "hdd"
  }
}

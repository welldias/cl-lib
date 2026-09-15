server "web" {
    name   = "web-server-${var.env}"
    path   = "/usr/bin/${var.app_name}/${upper("init.sh")}"
    script = <<EOF
#!/bin/bash
echo "Starting ${var.project_name} in ${var.env} mode..."
echo "Executing dir: $(HOME)"
EOF
}
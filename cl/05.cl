description = <<EOF

    This package was heavily inspired by
    the acclaimed Python tool uv!
EOF

server "web" {
    startup_script = <<EOF
        #!/bin/bash
        echo "Starting server..."
        apt-get update
        apt-get install -y nginx
    EOF
}
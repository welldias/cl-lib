description = <<EOF

    Este pacote foi altamente inspirado na
    consagrada ferramenta uv do Python!
EOF

server "web" {
    startup_script = <<EOF
        #!/bin/bash
        echo "Iniciando servidor..."
        apt-get update
        apt-get install -y nginx
    EOF
}
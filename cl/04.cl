var = {
  first_name = "Ada"
  last_name  = "Lovelace"
  aws_region = "east-2"
  server = {
    ips = ["127.0.0.1", "192.168.0.1"]
  }
}

enable_monitoring = true

# 1. Bloco de Configuração do Terraform
terraform {
  required_version = ">= 1.5.0"

  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
  }
}

# 2. Configuração do Provider
provider "aws" {
  region = var.aws_region
  enable = enable_monitoring
}

# 3. Definição de Variáveis
variable "aws_region" {
  description = "Região da AWS onde os recursos serão criados"
  type        = "string"
  default     = "us-east-1"
}

variable "instance_name" {
  description = "Valor para a tag Name da instância"
  #type        = string
  default     = "Servidor-Producao-01"
}

# 4. Recurso: Criação de uma Instância EC2
resource "aws_instance" {
  ami           = "ami-0c55b159cbfafe1f0" # Exemplo de ID de imagem (Ubuntu)
  instance_type = "t2.micro"

  tags = {
    Name        = variable.instance_name
    Environment = "Dev"
    Project     = "Modernizacao-TI"
  }
}

# 5. Output: Informação exibida após o 'terraform apply'
output "instance_public_ip" {
  description = "O endereço IP público da instância criada"
  value       = resource.aws_instance.ami
}
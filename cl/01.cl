# Configurações globais do Terraform
terraform {
  required_version = ">= 1.5.0"

  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
  }
}

# Configuração específica do provedor AWS
provider "aws" {
  region = var.aws_region

  # Default tags aplicadas a todos os recursos criados por este provider
  default_tags {
    tags = {
      Project   = "Infra-Core"
      ManagedBy = "Terraform"
    }
  }
}
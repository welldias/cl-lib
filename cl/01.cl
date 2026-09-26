# Global Terraform settings
terraform {
  required_version = ">= 1.5.0"

  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
  }
}

# AWS provider-specific settings
provider "aws" {
  region = var.aws_region

  # Default tags applied to every resource created by this provider
  default_tags {
    tags = {
      Project   = "Infra-Core"
      ManagedBy = "Terraform"
    }
  }
}
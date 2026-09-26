var = {
  first_name = "Ada"
  last_name  = "Lovelace"
  aws_region = "east-2"
  server = {
    ips = ["127.0.0.1", "192.168.0.1"]
  }
}

enable_monitoring = true

# 1. Terraform settings block
terraform {
  required_version = ">= 1.5.0"

  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
  }
}

# 2. Provider settings
provider "aws" {
  region = var.aws_region
  enable = enable_monitoring
}

# 3. Variable definitions
variable "aws_region" {
  description = "AWS region where the resources will be created"
  type        = "string"
  default     = "us-east-1"
}

variable "instance_name" {
  description = "Value for the instance's Name tag"
  #type        = string
  default     = "Production-Server-01"
}

# 4. Resource: creating an EC2 instance
resource "aws_instance" {
  ami           = "ami-0c55b159cbfafe1f0" # Example image ID (Ubuntu)
  instance_type = "t2.micro"

  tags = {
    Name        = variable.instance_name
    Environment = "Dev"
    Project     = "IT-Modernization"
  }
}

# 5. Output: information shown after 'terraform apply'
output "instance_public_ip" {
  description = "Public IP address of the created instance"
  value       = resource.aws_instance.ami
}
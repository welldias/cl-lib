variable "aws_region" {
  description = "AWS region to deploy the resources to"
  type        = "string"
  default     = "us-east-1"
}

variable "instance_type" {
  description = "EC2 instance type (e.g. t2.micro, t3.medium)"
  type        = "string"
  default     = "t3.small"
}

variable "project_name" {
  description = "Base name used to identify the resources"
  type        = "string"
}

variable "enable_monitoring" {
  description = "If true, enables detailed monitoring in CloudWatch"
  type        = "bool"
  default     = false
}
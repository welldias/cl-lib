variable "aws_region" {
  description = "Região da AWS para o deploy dos recursos"
  type        = string
  default     = "us-east-1"
}

variable "instance_type" {
  description = "Tipo da instância EC2 (ex: t2.micro, t3.medium)"
  type        = string
  default     = "t3.small"
}

variable "project_name" {
  description = "Nome base para identificação dos recursos"
  type        = string
}

variable "enable_monitoring" {
  description = "Se verdadeiro, habilita o monitoramento detalhado no CloudWatch"
  type        = bool
  default     = false
}
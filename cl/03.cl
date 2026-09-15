# "aws_instance" aqui e o proprio type do bloco de topo (nao um label
# dentro de um bloco "resource" como no Terraform) - e o que a resolucao
# agnostica da lib cl exige para "aws_instance.app_server.*" resolver.
aws_instance "app_server" {
  id        = "i-0abcd1234ef567890"
  public_ip = "203.0.113.42"
  arn       = "arn:aws:ec2:us-east-1:123456789012:instance/i-0abcd1234ef567890"
}

output "instance_id" {
  description = "O ID da instância EC2 criada"
  value       = aws_instance.app_server.id
}

output "instance_public_ip" {
  description = "O IP público da instância para acesso externo"
  value       = aws_instance.app_server.public_ip
}

output "instance_arn" {
  description = "O Amazon Resource Name (ARN) da instância"
  value       = aws_instance.app_server.arn
}

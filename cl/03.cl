# "aws_instance" here is the top-level block's own type (not a label
# inside a "resource" block as in Terraform) - that is what the cl library's
# name-agnostic resolution needs for "aws_instance.app_server.*" to resolve.
aws_instance "app_server" {
  id        = "i-0abcd1234ef567890"
  public_ip = "203.0.113.42"
  arn       = "arn:aws:ec2:us-east-1:123456789012:instance/i-0abcd1234ef567890"
}

output "instance_id" {
  description = "ID of the created EC2 instance"
  value       = aws_instance.app_server.id
}

output "instance_public_ip" {
  description = "Public IP of the instance for external access"
  value       = aws_instance.app_server.public_ip
}

output "instance_arn" {
  description = "Amazon Resource Name (ARN) of the instance"
  value       = aws_instance.app_server.arn
}

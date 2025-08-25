ssh ubuntu@api.muchq.com << EOF
  sudo docker compose pull;
  sudo docker compose up -d;
EOF


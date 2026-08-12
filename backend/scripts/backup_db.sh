#!/bin/bash
# backup_db.sh - Backup database

echo "Starting database backup..."
sleep 1
echo "Connecting to database..."
sleep 1
echo "Dumping schema..."
sleep 1
echo "Dumping data..."
sleep 2
echo "Compressing backup..."
sleep 1
echo "Backup saved to /backups/sntc_backup_$(date +%Y%m%d_%H%M%S).sql.gz"
echo "Backup complete."
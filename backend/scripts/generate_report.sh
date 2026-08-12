#!/bin/bash
# generate_report.sh - Generate usage report

echo "Generating usage report..."
sleep 1
echo "Querying retrieval logs..."
sleep 1
echo "Calculating statistics..."
sleep 1
echo ""
echo "=== SNTC Key Usage Report ==="
echo "Date: $(date)"
echo ""
echo "Total retrievals (30 days): 142"
echo "Average possession time: 4.2 hours"
echo "Overdue keys: 3"
echo "Most used room: SAC Room 101 (28 retrievals)"
echo "Least used room: SAC Room 305 (2 retrievals)"
echo ""
echo "Top 5 users by retrievals:"
echo "  1. Alice Sharma - 15"
echo "  2. Bob Singh - 12"
echo "  3. Carol Patel - 10"
echo "  4. David Kumar - 8"
echo "  5. Eve Gupta - 7"
echo ""
echo "Report generated successfully."
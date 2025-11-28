# Start backend and frontend services

Write-Host "Starting EverAsk Services..." -ForegroundColor Green

# Start backend in background
Write-Host "Starting Flask backend on port 5000..." -ForegroundColor Yellow
Start-Process powershell -ArgumentList "-Command", "cd 'D:\Code\Languages\Arduino\Exhibition\backend'; python main.py"

# Wait a moment for backend to start
Start-Sleep -Seconds 3

# Start frontend in background  
Write-Host "Starting Next.js frontend on port 3000..." -ForegroundColor Yellow
Start-Process powershell -ArgumentList "-Command", "cd 'D:\Code\Languages\Arduino\Exhibition\everask'; npm run dev"

Write-Host "Services starting..." -ForegroundColor Green
Write-Host "Backend: http://localhost:5000" -ForegroundColor Cyan
Write-Host "Frontend: http://localhost:3000" -ForegroundColor Cyan
Write-Host "Press any key to continue..." -ForegroundColor White
Read-Host
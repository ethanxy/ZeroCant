#!/bin/bash

# ESP-IDF Build and Flash Script
# This script assumes ESP-IDF environment is properly set up

echo "🔨 Building ESP-IDF project..."

# Try different ways to find idf.py
if command -v idf.py &> /dev/null; then
    IDF_PY="idf.py"
elif [ -f "$IDF_PATH/tools/idf.py" ]; then
    IDF_PY="$IDF_PATH/tools/idf.py"
elif [ -f "/opt/esp/idf/tools/idf.py" ]; then
    IDF_PY="/opt/esp/idf/tools/idf.py"
else
    echo "❌ Error: idf.py not found. Make sure ESP-IDF is properly installed and sourced."
    echo "Try running: source ~/esp/esp-idf/export.sh"
    exit 1
fi

echo "📍 Using IDF tool: $IDF_PY"

# Build the project
echo "🔧 Building project..."
$IDF_PY build

if [ $? -eq 0 ]; then
    echo "✅ Build successful!"
    echo "🔧 Flashing to device..."
    $IDF_PY flash
    
    if [ $? -eq 0 ]; then
        echo "✅ Flash successful!"
        echo "📺 Starting monitor..."
        $IDF_PY monitor
    else
        echo "❌ Flash failed!"
        exit 1
    fi
else
    echo "❌ Build failed!"
    exit 1
fi

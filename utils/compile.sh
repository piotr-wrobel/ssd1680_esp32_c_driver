rm -rf build
mkdir -p build
for I in *.c; do
echo "compile $I..."
gcc  "$I" -o "build/"`echo $I | sed -e 's/.c//g'`
done

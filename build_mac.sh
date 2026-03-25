#!/bin/bash
APP_NAME="ANNEWizard"
BUILD_DIR="build"
QT_DIR="${QT6_DIR:-/Users/user/Qt/6.10.1/macos}"
DEPLOYMENT_TARGET="10.15"

echo "========================================="
echo "Building $APP_NAME for macOS..."
echo "========================================="

if [ -d "$BUILD_DIR" ]; then
    rm -rf "$BUILD_DIR"
fi

cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSTATIC_BUILD=OFF \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOYMENT_TARGET" \
    -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
    -DCMAKE_PREFIX_PATH="$QT_DIR"


cmake --build "$BUILD_DIR" --parallel 3

APP_BUNDLE="$BUILD_DIR/$APP_NAME.app"
if [ ! -d "$APP_BUNDLE" ]; then
    echo "ERROR: App bundle not found at $APP_BUNDLE"
    exit 1
fi

echo "✓ Build complete: $APP_BUNDLE"

echo ""
echo "Verifying architectures..."
lipo -info "$APP_BUNDLE/Contents/MacOS/$APP_NAME"


echo ""
echo "Stripping executable..."
strip -S "$APP_BUNDLE/Contents/MacOS/$APP_NAME"


echo ""
echo "Running macdeployqt to bundle frameworks..."

if [ -f "$QT_DIR/bin/macdeployqt" ]; then
    "$QT_DIR/bin/macdeployqt" "$APP_BUNDLE" -verbose=1
    if [ $? -ne 0 ]; then
        echo "ERROR: macdeployqt failed"
        exit 1
    fi
else
    echo "ERROR: macdeployqt not found at $QT_DIR/bin/macdeployqt"
    exit 1
fi


echo ""
if [ -d "$APP_BUNDLE/Contents/Frameworks" ]; then
    FRAMEWORK_COUNT=$(ls -1 "$APP_BUNDLE/Contents/Frameworks" | wc -l)
    echo "✓ Frameworks bundled: $FRAMEWORK_COUNT frameworks"
else
    echo "⚠ WARNING: No frameworks found in app bundle"
fi

echo ""
echo "Stripping frameworks and libraries..."

if [ -d "$APP_BUNDLE/Contents/Frameworks" ]; then
    find "$APP_BUNDLE/Contents/Frameworks" -type f | while read file; do
        if file "$file" | grep -q "Mach-O"; then
            BEFORE=$(stat -f %z "$file")
            strip -S "$file" 2>/dev/null || true
            AFTER=$(stat -f %z "$file")
            if [ "$BEFORE" != "$AFTER" ]; then
                echo "  Stripped: $(basename "$file") ($(( (BEFORE - AFTER) / 1024 ))KB saved)"
            fi
        fi
    done
fi

if [ -d "$APP_BUNDLE/Contents/PlugIns" ]; then
    echo ""
    echo "Stripping plugins..."
    find "$APP_BUNDLE/Contents/PlugIns" -type f -name "*.dylib" | while read file; do
        if file "$file" | grep -q "Mach-O"; then
            strip -S "$file" 2>/dev/null || true
        fi
    done
fi


if [ -d "assets" ]; then
    echo ""
    echo "Copying assets..."
    mkdir -p "$APP_BUNDLE/Contents/Resources"
    cp -r assets/* "$APP_BUNDLE/Contents/Resources/" 2>/dev/null || true
fi

chmod +x "$APP_BUNDLE/Contents/MacOS/$APP_NAME"


echo ""
echo "========================================="
echo "Final app bundle size breakdown:"
du -sh "$APP_BUNDLE"
if [ -d "$APP_BUNDLE/Contents/Frameworks" ]; then
    du -sh "$APP_BUNDLE/Contents/Frameworks"
fi
du -sh "$APP_BUNDLE/Contents/MacOS"
echo "========================================="


echo ""
echo "Creating DMG installer..."

DMG_CONTENTS="$BUILD_DIR/dmg_contents"
rm -rf "$DMG_CONTENTS"
mkdir -p "$DMG_CONTENTS"

cp -R "$APP_BUNDLE" "$DMG_CONTENTS/"
ln -s /Applications "$DMG_CONTENTS/Applications"

DMG_NAME="$BUILD_DIR/${APP_NAME}-${DEPLOYMENT_TARGET}.dmg"
hdiutil create -volname "$APP_NAME Installer" \
    -srcfolder "$DMG_CONTENTS" \
    -ov -format UDZO \
    -fs HFS+ \
    -imagekey zlib-level=9 \
    "$DMG_NAME"

rm -rf "$DMG_CONTENTS"

if [ -f "$DMG_NAME" ]; then
    echo "✓ DMG created: $DMG_NAME"
    du -sh "$DMG_NAME"
else
    echo "✗ Failed to create DMG"
fi

echo ""
echo "========================================="
echo "Deployment complete!"
echo "App bundle: $APP_BUNDLE"
if [ -f "$DMG_NAME" ]; then
    echo "DMG: $DMG_NAME"
fi
echo ""
echo "To test: open $APP_BUNDLE"
echo "========================================="
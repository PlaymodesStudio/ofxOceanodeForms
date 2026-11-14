#!/bin/bash
# Compiles formsBox.scd into CompiledSynthdefs/Forms with the bundled sclang of ofxOceanodeSuperCollider
# (its class library and extensions, the Oceanode pseudo-UGens, and FormsSparseBank's class).
set -e
cd "$(dirname "$0")"
ADDONS="$(cd ../.. && pwd)"
SCL="$ADDONS/ofxOceanodeSuperCollider"
CONF="$(mktemp -t forms_sclang_conf).yaml"
cat > "$CONF" <<YAML
includePaths:
    - $SCL/sclang/SCClassLibrary
    - $SCL/sclang/Extensions
    - $SCL/synthdefs/Oceanode
    - $ADDONS/ofxOceanodeForms/ugens/FormsSparseBank
excludePaths:
    []
postInlineWarnings: false
excludeDefaultPaths: true
YAML
"$SCL/sclang/osx/sclang" -l "$CONF" "$(pwd)/formsBox.scd"
rm -f "$CONF"

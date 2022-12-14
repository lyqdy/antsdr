#!/bin/bash

VIVADO_VER=2019.1
DISPLAY_NAME="USRP-X3x0"
REPO_BASE_PATH=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)

declare -A PRODUCT_ID_MAP
PRODUCT_ID_MAP["X300"]="kintex7/xc7k325t/ffg900/-2"
PRODUCT_ID_MAP["X310"]="kintex7/xc7k410t/ffg900/-2"

source $REPO_BASE_PATH/tools/scripts/setupenv_base.sh

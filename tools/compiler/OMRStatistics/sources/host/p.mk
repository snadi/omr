################################################################################
##
## (c) Copyright IBM Corp. 2016, 2017
##
##  This program and the accompanying materials are made available
##  under the terms of the Eclipse Public License v1.0 and
##  Apache License v2.0 which accompanies this distribution.
##
##      The Eclipse Public License is available at
##      http://www.eclipse.org/legal/epl-v10.html
##
##      The Apache License v2.0 is available at
##      http://www.opensource.org/licenses/apache2.0.php
##
## Contributors:
##    Multiple authors (IBM Corp.) - initial implementation and documentation
################################################################################


JIT_PRODUCT_SOURCE_FILES+=\
    "$(JIT_PRODUCT_DIR)/p/runtime/AsmUtil.spp" \
    "$(JIT_PRODUCT_DIR)/p/runtime/CodeDispatch.spp" \
    "$(JIT_PRODUCT_DIR)/p/runtime/CodeSync.cpp" \
    "$(JIT_OMR_DIRTY_DIR)/p/runtime/OMRCodeCacheConfig.cpp"

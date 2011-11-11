ifeq ($(shell uname -s),Darwin)

HDIUTIL = hdiutil

DMG_TMPDIR = $(TARGET_OUTPUT_DIR)/dmg
DMG_TMP = $(TARGET_OUTPUT_DIR)/tmp.dmg

$(DMG_TMP): $(TARGET_BIN_DIR)/xcsoar build/Info.plist output/data/graphics/xcsoarswiftsplash_128.icns
	@$(NQ)echo "  DMG     $@"
	$(Q)rm -rf $(DMG_TMPDIR)
	$(Q)mkdir -p $(DMG_TMPDIR)/XCSoar.app/Contents/MacOS
	$(Q)sed -e "s,VERSION,$(FULL_VERSION)," <build/Info.plist >$(DMG_TMPDIR)/XCSoar.app/Contents/Info.plist
	$(Q)cp $(TARGET_BIN_DIR)/xcsoar $(DMG_TMPDIR)/XCSoar.app/Contents/MacOS/
	$(Q)mkdir -p $(DMG_TMPDIR)/XCSoar.app/Contents/Resources
	$(Q)cp output/data/graphics/xcsoarswiftsplash_128.icns $(DMG_TMPDIR)/XCSoar.app/Contents/Resources/
	$(Q)$(HDIUTIL) create -fs HFS+ -volname "XCSoar" -srcfolder $(DMG_TMPDIR) $@

$(TARGET_OUTPUT_DIR)/XCSoar.dmg: $(DMG_TMP)
	@$(NQ)echo "  DMG     $@"
	$(Q)$(HDIUTIL) convert $< -format UDCO -o $@
	$(Q)$(HDIUTIL) internet-enable -yes $@

endif

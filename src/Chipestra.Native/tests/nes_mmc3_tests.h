// MMC3 (mapper 4) support of the NES Machine, called from native_lifecycle_tests.cpp's main(). The synthetic
// tests use a hand-written 64 KB program with 8 KB banks; the real-ROM test (CHIPESTRA_ROM_ROOT and
// CHIPESTRA_REFERENCE_DIR) plays every track of smb3.xml and compares the APU writes with a recording of the
// real game.

void Mmc3PrgMapsBanksLikeTheOriginal();
void NesMmc3CartridgeAndDriverSettings();
void NesMachineMmc3BankingWramAndDmcReads();
void Smb3ViaDefinitionXmlRealRomMatchesTheRealGame();

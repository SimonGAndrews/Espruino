echo(false);
var Storage = require("Storage");
var phaseFile = "xfc_save_phase";
var exitFile = "xfc_reset_exit";
var noExit = Storage.read(exitFile) === undefined;
var noSavedImage = Storage.list().indexOf(".varimg") < 0;
var passed = noExit && noSavedImage;

print("TEST=xfsm_reset_lifecycle");
print((noExit ? "PASS " : "FAIL ") + "reset_without_exit");
print((noSavedImage ? "PASS " : "FAIL ") + "saved_image_erased");
print("DONE=" + (passed ? "PASS" : "FAIL"));

Storage.erase(phaseFile);
Storage.erase(exitFile);

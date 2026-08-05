# Git archive substitutes these values for an exported release tree. Ordinary
# Git checkouts ignore this file because QuarryVersion.cmake resolves from Git
# history first.
set(QUARRY_ARCHIVE_TAG "$Format:%(describe:tags)$")
set(QUARRY_GIT_SHA "$Format:%H$")

//
// Copyright 2006 The Android Open Source Project
//

#include "AaptAssets.h"
#include "ResourceFilter.h"
#include "Main.h"

#include <utils/misc.h>
#include <utils/SortedVector.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>

static const char* kDefaultLocale = "default";
static const char* kWildcardName = "any";
static const char* kAssetDir = "assets";
static const char* kResourceDir = "res";
static const char* kValuesDir = "values";
static const char* kMipmapDir = "mipmap";
static const char* kInvalidChars = "/\\:";
static const size_t kMaxAssetFileName = 100;

static const String8 kResString(kResourceDir);

/*
 * Names of asset files must meet the following criteria:
 *
 *  - the filename length must be less than kMaxAssetFileName bytes long
 *    (and can't be empty)
 *  - all characters must be 7-bit printable ASCII
 *  - none of { '/' '\\' ':' }
 *
 * Pass in just the filename, not the full path.
 */
static bool validateFileName(const char* fileName)
{
    const char* cp = fileName;
    size_t len = 0;

    while (*cp != '\0') {
        if ((*cp & 0x80) != 0)
            return false;           // reject high ASCII
        if (*cp < 0x20 || *cp >= 0x7f)
            return false;           // reject control chars and 0x7f
        if (strchr(kInvalidChars, *cp) != NULL)
            return false;           // reject path sep chars
        cp++;
        len++;
    }

    if (len < 1 || len > kMaxAssetFileName)
        return false;               // reject empty or too long

    return true;
}

// The default to use if no other ignore pattern is defined.
const char * const gDefaultIgnoreAssets =
    "!.svn:!.git:!.ds_store:!*.scc:.*:<dir>_*:!CVS:!thumbs.db:!picasa.ini:!*~";
// The ignore pattern that can be passed via --ignore-assets in Main.cpp
const char * gUserIgnoreAssets = NULL;

static bool isHidden(const char *root, const char *path)
{
    // Patterns syntax:
    // - Delimiter is :
    // - Entry can start with the flag ! to avoid printing a warning
    //   about the file being ignored.
    // - Entry can have the flag "<dir>" to match only directories
    //   or <file> to match only files. Default is to match both.
    // - Entry can be a simplified glob "<prefix>*" or "*<suffix>"
    //   where prefix/suffix must have at least 1 character (so that
    //   we don't match a '*' catch-all pattern.)
    // - The special filenames "." and ".." are always ignored.
    // - Otherwise the full string is matched.
    // - match is not case-sensitive.

    if (strcmp(path, ".") == 0 || strcmp(path, "..") == 0) {
        return true;
    }

    const char *delim = ":";
    const char *p = gUserIgnoreAssets;
    if (!p || !p[0]) {
        p = getenv("ANDROID_AAPT_IGNORE");
    }
    if (!p || !p[0]) {
        p = gDefaultIgnoreAssets;
    }
    char *patterns = strdup(p);

    bool ignore = false;
    bool chatty = true;
    char *matchedPattern = NULL;

    String8 fullPath(root);
    fullPath.appendPath(path);
    FileType type = getFileType(fullPath);

    int plen = strlen(path);

    // Note: we don't have strtok_r under mingw.
    for(char *token = strtok(patterns, delim);
            !ignore && token != NULL;
            token = strtok(NULL, delim)) {
        chatty = token[0] != '!';
        if (!chatty) token++; // skip !
        if (strncasecmp(token, "<dir>" , 5) == 0) {
            if (type != kFileTypeDirectory) continue;
            token += 5;
        }
        if (strncasecmp(token, "<file>", 6) == 0) {
            if (type != kFileTypeRegular) continue;
            token += 6;
        }

        matchedPattern = token;
        int n = strlen(token);

        if (token[0] == '*') {
            // Match *suffix
            token++;
            n--;
            if (n <= plen) {
                ignore = strncasecmp(token, path + plen - n, n) == 0;
            }
        } else if (n > 1 && token[n - 1] == '*') {
            // Match prefix*
            ignore = strncasecmp(token, path, n - 1) == 0;
        } else {
            ignore = strcasecmp(token, path) == 0;
        }
    }

    if (ignore && chatty) {
        fprintf(stderr, "    (skipping %s '%s' due to ANDROID_AAPT_IGNORE pattern '%s')\n",
                type == kFileTypeDirectory ? "dir" : "file",
                path,
                matchedPattern ? matchedPattern : "");
    }

    free(patterns);
    return ignore;
}

// =========================================================================
// =========================================================================
// =========================================================================

status_t
AaptGroupEntry::parseNamePart(const String8& part, int* axis, uint32_t* value)
{
    ResTable_config config;

    // IMSI - MCC
    if (getMccName(part.string(), &config)) {
        *axis = AXIS_MCC;
        *value = config.mcc;
        return 0;
    }

    // IMSI - MNC
    if (getMncName(part.string(), &config)) {
        *axis = AXIS_MNC;
        *value = config.mnc;
        return 0;
    }

    // locale - language
    if (part.length() == 2 && isalpha(part[0]) && isalpha(part[1])) {
        *axis = AXIS_LANGUAGE;
        *value = part[1] << 8 | part[0];
        return 0;
    }

    // locale - language_REGION
    if (part.length() == 5 && isalpha(part[0]) && isalpha(part[1])
            && part[2] == '_' && isalpha(part[3]) && isalpha(part[4])) {
        *axis = AXIS_LANGUAGE;
        *value = (part[4] << 24) | (part[3] << 16) | (part[1] << 8) | (part[0]);
        return 0;
    }

    // layout direction
    if (getLayoutDirectionName(part.string(), &config)) {
        *axis = AXIS_LAYOUTDIR;
        *value = (config.screenLayout&ResTable_config::MASK_LAYOUTDIR);
        return 0;
    }

    // smallest screen dp width
    if (getSmallestScreenWidthDpName(part.string(), &config)) {
        *axis = AXIS_SMALLESTSCREENWIDTHDP;
        *value = config.smallestScreenWidthDp;
        return 0;
    }

    // screen dp width
    if (getScreenWidthDpName(part.string(), &config)) {
        *axis = AXIS_SCREENWIDTHDP;
        *value = config.screenWidthDp;
        return 0;
    }

    // screen dp height
    if (getScreenHeightDpName(part.string(), &config)) {
        *axis = AXIS_SCREENHEIGHTDP;
        *value = config.screenHeightDp;
        return 0;
    }

    // screen layout size
    if (getScreenLayoutSizeName(part.string(), &config)) {
        *axis = AXIS_SCREENLAYOUTSIZE;
        *value = (config.screenLayout&ResTable_config::MASK_SCREENSIZE);
        return 0;
    }

    // screen layout long
    if (getScreenLayoutLongName(part.string(), &config)) {
        *axis = AXIS_SCREENLAYOUTLONG;
        *value = (config.screenLayout&ResTable_config::MASK_SCREENLONG);
        return 0;
    }

    // orientation
    if (getOrientationName(part.string(), &config)) {
        *axis = AXIS_ORIENTATION;
        *value = config.orientation;
        return 0;
    }

    // ui inverted mode
    if (getUiInvertedModeName(part.string(), &config)) {
        *axis = AXIS_UIINVERTEDMODE;
        *value = config.uiInvertedMode;
        return 0;
    }

    // ui mode type
    if (getUiModeTypeName(part.string(), &config)) {
        *axis = AXIS_UIMODETYPE;
        *value = (config.uiMode&ResTable_config::MASK_UI_MODE_TYPE);
        return 0;
    }

    // ui mode night
    if (getUiModeNightName(part.string(), &config)) {
        *axis = AXIS_UIMODENIGHT;
        *value = (config.uiMode&ResTable_config::MASK_UI_MODE_NIGHT);
        return 0;
    }

    // density
    if (getDensityName(part.string(), &config)) {
        *axis = AXIS_DENSITY;
        *value = config.density;
        return 0;
    }

    // touchscreen
    if (getTouchscreenName(part.string(), &config)) {
        *axis = AXIS_TOUCHSCREEN;
        *value = config.touchscreen;
        return 0;
    }

    // keyboard hidden
    if (getKeysHiddenName(part.string(), &config)) {
        *axis = AXIS_KEYSHIDDEN;
        *value = config.inputFlags;
        return 0;
    }

    // keyboard
    if (getKeyboardName(part.string(), &config)) {
        *axis = AXIS_KEYBOARD;
        *value = config.keyboard;
        return 0;
    }

    // navigation hidden
    if (getNavHiddenName(part.string(), &config)) {
        *axis = AXIS_NAVHIDDEN;
        *value = config.inputFlags;
        return 0;
    }

    // navigation
    if (getNavigationName(part.string(), &config)) {
        *axis = AXIS_NAVIGATION;
        *value = config.navigation;
        return 0;
    }

    // screen size
    if (getScreenSizeName(part.string(), &config)) {
        *axis = AXIS_SCREENSIZE;
        *value = config.screenSize;
        return 0;
    }

    // version
    if (getVersionName(part.string(), &config)) {
        *axis = AXIS_VERSION;
        *value = config.version;
        return 0;
    }

    return 1;
}

uint32_t
AaptGroupEntry::getConfigValueForAxis(const ResTable_config& config, int axis)
{
    switch (axis) {
        case AXIS_MCC:
            return config.mcc;
        case AXIS_MNC:
            return config.mnc;
        case AXIS_LANGUAGE:
            return (((uint32_t)config.country[1]) << 24) | (((uint32_t)config.country[0]) << 16)
                | (((uint32_t)config.language[1]) << 8) | (config.language[0]);
        case AXIS_LAYOUTDIR:
            return config.screenLayout&ResTable_config::MASK_LAYOUTDIR;
        case AXIS_SCREENLAYOUTSIZE:
            return config.screenLayout&ResTable_config::MASK_SCREENSIZE;
        case AXIS_ORIENTATION:
            return config.orientation;
        case AXIS_UIINVERTEDMODE:
            return config.uiInvertedMode;
        case AXIS_UIMODETYPE:
            return (config.uiMode&ResTable_config::MASK_UI_MODE_TYPE);
        case AXIS_UIMODENIGHT:
            return (config.uiMode&ResTable_config::MASK_UI_MODE_NIGHT);
        case AXIS_DENSITY:
            return config.density;
        case AXIS_TOUCHSCREEN:
            return config.touchscreen;
        case AXIS_KEYSHIDDEN:
            return config.inputFlags;
        case AXIS_KEYBOARD:
            return config.keyboard;
        case AXIS_NAVIGATION:
            return config.navigation;
        case AXIS_SCREENSIZE:
            return config.screenSize;
        case AXIS_SMALLESTSCREENWIDTHDP:
            return config.smallestScreenWidthDp;
        case AXIS_SCREENWIDTHDP:
            return config.screenWidthDp;
        case AXIS_SCREENHEIGHTDP:
            return config.screenHeightDp;
        case AXIS_VERSION:
            return config.version;
    }
    return 0;
}

bool
AaptGroupEntry::configSameExcept(const ResTable_config& config,
        const ResTable_config& otherConfig, int axis)
{
    for (int i=AXIS_START; i<=AXIS_END; i++) {
        if (i == axis) {
            continue;
        }
        if (getConfigValueForAxis(config, i) != getConfigValueForAxis(otherConfig, i)) {
            return false;
        }
    }
    return true;
}

bool
AaptGroupEntry::initFromDirName(const char* dir, String8* resType)
{
    mParamsChanged = true;

    Vector<String8> parts;

    String8 mcc, mnc, loc, layoutsize, layoutlong, orient, den;
    String8 touch, key, keysHidden, nav, navHidden, size, layoutDir, vers;
    String8 uiInvertedMode, uiModeType, uiModeNight, smallestwidthdp, widthdp, heightdp;

    const char *p = dir;
    const char *q;
    while (NULL != (q = strchr(p, '-'))) {
        String8 val(p, q-p);
        val.toLower();
        parts.add(val);
        //printf("part: %s\n", parts[parts.size()-1].string());
        p = q+1;
    }
    String8 val(p);
    val.toLower();
    parts.add(val);
    //printf("part: %s\n", parts[parts.size()-1].string());

    const int N = parts.size();
    int index = 0;
    String8 part = parts[index];

    // resource type
    if (!isValidResourceType(part)) {
        return false;
    }
    *resType = part;

    index++;
    if (index == N) {
        goto success;
    }
    part = parts[index];

    // imsi - mcc
    if (getMccName(part.string())) {
        mcc = part;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("not mcc: %s\n", part.string());
    }

    // imsi - mnc
    if (getMncName(part.string())) {
        mnc = part;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("not mcc: %s\n", part.string());
    }

    // locale - language
    if (part.length() == 2 && isalpha(part[0]) && isalpha(part[1])) {
        loc = part;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("not language: %s\n", part.string());
    }

    // locale - region
    if (loc.length() > 0
            && part.length() == 3 && part[0] == 'r' && part[0] && part[1]) {
        loc += "-";
        part.toUpper();
        loc += part.string() + 1;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("not region: %s\n", part.string());
    }

    if (getLayoutDirectionName(part.string())) {
        layoutDir = part;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("not layout direction: %s\n", part.string());
    }

    if (getSmallestScreenWidthDpName(part.string())) {
        smallestwidthdp = part;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("not smallest screen width dp: %s\n", part.string());
    }

    if (getScreenWidthDpName(part.string())) {
        widthdp = part;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("not screen width dp: %s\n", part.string());
    }

    if (getScreenHeightDpName(part.string())) {
        heightdp = part;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("not screen height dp: %s\n", part.string());
    }

    if (getScreenLayoutSizeName(part.string())) {
        layoutsize = part;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("not screen layout size: %s\n", part.string());
    }

    if (getScreenLayoutLongName(part.string())) {
        layoutlong = part;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("not screen layout long: %s\n", part.string());
    }

    // orientation
    if (getOrientationName(part.string())) {
        orient = part;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("not orientation: %s\n", part.string());
    }

    // ui inverted mode
    if (getUiInvertedModeName(part.string())) {
        uiInvertedMode = part;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("no ui inverted mode: %s\n", part.string());
    }

    // ui mode type
    if (getUiModeTypeName(part.string())) {
        uiModeType = part;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("not ui mode type: %s\n", part.string());
    }

    // ui mode night
    if (getUiModeNightName(part.string())) {
        uiModeNight = part;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("not ui mode night: %s\n", part.string());
    }

    // density
    if (getDensityName(part.string())) {
        den = part;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("not density: %s\n", part.string());
    }

    // touchscreen
    if (getTouchscreenName(part.string())) {
        touch = part;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("not touchscreen: %s\n", part.string());
    }

    // keyboard hidden
    if (getKeysHiddenName(part.string())) {
        keysHidden = part;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("not keysHidden: %s\n", part.string());
    }

    // keyboard
    if (getKeyboardName(part.string())) {
        key = part;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("not keyboard: %s\n", part.string());
    }

    // navigation hidden
    if (getNavHiddenName(part.string())) {
        navHidden = part;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("not navHidden: %s\n", part.string());
    }

    if (getNavigationName(part.string())) {
        nav = part;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("not navigation: %s\n", part.string());
    }

    if (getScreenSizeName(part.string())) {
        size = part;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("not screen size: %s\n", part.string());
    }

    if (getVersionName(part.string())) {
        vers = part;

        index++;
        if (index == N) {
            goto success;
        }
        part = parts[index];
    } else {
        //printf("not version: %s\n", part.string());
    }

    // if there are extra parts, it doesn't match
    return false;

success:
    this->mcc = mcc;
    this->mnc = mnc;
    this->locale = loc;
    this->screenLayoutSize = layoutsize;
    this->screenLayoutLong = layoutlong;
    this->smallestScreenWidthDp = smallestwidthdp;
    this->screenWidthDp = widthdp;
    this->screenHeightDp = heightdp;
    this->orientation = orient;
    this->uiInvertedMode = uiInvertedMode;
    this->uiModeType = uiModeType;
    this->uiModeNight = uiModeNight;
    this->density = den;
    this->touchscreen = touch;
    this->keysHidden = keysHidden;
    this->keyboard = key;
    this->navHidden = navHidden;
    this->navigation = nav;
    this->screenSize = size;
    this->layoutDirection = layoutDir;
    this->version = vers;

    // what is this anyway?
    this->vendor = "";

    return true;
}

String8
AaptGroupEntry::toString() const
{
    String8 s = this->mcc;
    s += ",";
    s += this->mnc;
    s += ",";
    s += this->locale;
    s += ",";
    s += layoutDirection;
    s += ",";
    s += smallestScreenWidthDp;
    s += ",";
    s += screenWidthDp;
    s += ",";
    s += screenHeightDp;
    s += ",";
    s += screenLayoutSize;
    s += ",";
    s += screenLayoutLong;
    s += ",";
    s += this->orientation;
    s += ",";
    s += uiInvertedMode;
    s += ",";
    s += uiModeType;
    s += ",";
    s += uiModeNight;
    s += ",";
    s += density;
    s += ",";
    s += touchscreen;
    s += ",";
    s += keysHidden;
    s += ",";
    s += keyboard;
    s += ",";
    s += navHidden;
    s += ",";
    s += navigation;
    s += ",";
    s += screenSize;
    s += ",";
    s += version;
    return s;
}

String8
AaptGroupEntry::toDirName(const String8& resType) const
{
    String8 s = resType;
    if (this->mcc != "") {
        if (s.length() > 0) {
            s += "-";
        }
        s += mcc;
    }
    if (this->mnc != "") {
        if (s.length() > 0) {
            s += "-";
        }
        s += mnc;
    }
    if (this->locale != "") {
        if (s.length() > 0) {
            s += "-";
        }
        s += locale;
    }
    if (this->layoutDirection != "") {
        if (s.length() > 0) {
            s += "-";
        }
        s += layoutDirection;
    }
    if (this->smallestScreenWidthDp != "") {
        if (s.length() > 0) {
            s += "-";
        }
        s += smallestScreenWidthDp;
    }
    if (this->screenWidthDp != "") {
        if (s.length() > 0) {
            s += "-";
        }
        s += screenWidthDp;
    }
    if (this->screenHeightDp != "") {
        if (s.length() > 0) {
            s += "-";
        }
        s += screenHeightDp;
    }
    if (this->screenLayoutSize != "") {
        if (s.length() > 0) {
            s += "-";
        }
        s += screenLayoutSize;
    }
    if (this->screenLayoutLong != "") {
        if (s.length() > 0) {
            s += "-";
        }
        s += screenLayoutLong;
    }
    if (this->orientation != "") {
        if (s.length() > 0) {
            s += "-";
        }
        s += orientation;
    }
    if (this->uiInvertedMode != "") {
        if (s.length() > 0) {
            s += "-";
        }
        s += uiInvertedMode;
    }
    if (this->uiModeType != "") {
        if (s.length() > 0) {
            s += "-";
        }
        s += uiModeType;
    }
    if (this->uiModeNight != "") {
        if (s.length() > 0) {
            s += "-";
        }
        s += uiModeNight;
    }
    if (this->density != "") {
        if (s.length() > 0) {
            s += "-";
        }
        s += density;
    }
    if (this->touchscreen != "") {
        if (s.length() > 0) {
            s += "-";
        }
        s += touchscreen;
    }
    if (this->keysHidden != "") {
        if (s.length() > 0) {
            s += "-";
        }
        s += keysHidden;
    }
    if (this->keyboard != "") {
        if (s.length() > 0) {
            s += "-";
        }
        s += keyboard;
    }
    if (this->navHidden != "") {
        if (s.length() > 0) {
            s += "-";
        }
        s += navHidden;
    }
    if (this->navigation != "") {
        if (s.length() > 0) {
            s += "-";
        }
        s += navigation;
    }
    if (this->screenSize != "") {
        if (s.length() > 0) {
            s += "-";
        }
        s += screenSize;
    }
    if (this->version != "") {
        if (s.length() > 0) {
            s += "-";
        }
        s += version;
    }

    return s;
}

bool AaptGroupEntry::getMccName(const char* name,
                                    ResTable_config* out)
{
    if (strcmp(name, kWildcardName) == 0) {
        if (out) out->mcc = 0;
        return true;
    }
    const char* c = name;
    if (tolower(*c) != 'm') return false;
    c++;
    if (tolower(*c) != 'c') return false;
    c++;
    if (tolower(*c) != 'c') return false;
    c++;

    const char* val = c;

    while (*c >= '0' && *c <= '9') {
        c++;
    }
    if (*c != 0) return false;
    if (c-val != 3) return false;

    int d = atoi(val);
    if (d != 0) {
        if (out) out->mcc = d;
        return true;
    }

    return false;
}

bool AaptGroupEntry::getMncName(const char* name,
                                    ResTable_config* out)
{
    if (strcmp(name, kWildcardName) == 0) {
        if (out) out->mcc = 0;
        return true;
    }
    const char* c = name;
    if (tolower(*c) != 'm') return false;
    c++;
    if (tolower(*c) != 'n') return false;
    c++;
    if (tolower(*c) != 'c') return false;
    c++;

    const char* val = c;

    while (*c >= '0' && *c <= '9') {
        c++;
    }
    if (*c != 0) return false;
    if (c-val == 0 || c-val > 3) return false;

    if (out) {
        out->mnc = atoi(val);
    }

    return true;
}

/*
 * Does this directory name fit the pattern of a locale dir ("en-rUS" or
 * "default")?
 *
 * TODO: Should insist that the first two letters are lower case, and the
 * second two are upper.
 */
bool AaptGroupEntry::getLocaleName(const char* fileName,
                                   ResTable_config* out)
{
    if (strcmp(fileName, kWildcardName) == 0
            || strcmp(fileName, kDefaultLocale) == 0) {
        if (out) {
            out->language[0] = 0;
            out->language[1] = 0;
            out->country[0] = 0;
            out->country[1] = 0;
        }
        return true;
    }

    if (strlen(fileName) == 2 && isalpha(fileName[0]) && isalpha(fileName[1])) {
        if (out) {
            out->language[0] = fileName[0];
            out->language[1] = fileName[1];
            out->country[0] = 0;
            out->country[1] = 0;
        }
        return true;
    }

    if (strlen(fileName) == 5 &&
        isalpha(fileName[0]) &&
        isalpha(fileName[1]) &&
        fileName[2] == '-' &&
        isalpha(fileName[3]) &&
        isalpha(fileName[4])) {
        if (out) {
            out->language[0] = fileName[0];
            out->language[1] = fileName[1];
            out->country[0] = fileName[3];
            out->country[1] = fileName[4];
        }
        return true;
    }

    return false;
}

bool AaptGroupEntry::getLayoutDirectionName(const char* name, ResTable_config* out)
{
    if (strcmp(name, kWildcardName) == 0) {
        if (out) out->screenLayout =
                (out->screenLayout&~ResTable_config::MASK_LAYOUTDIR)
                | ResTable_config::LAYOUTDIR_ANY;
        return true;
    } else if (strcmp(name, "ldltr") == 0) {
        if (out) out->screenLayout =
                (out->screenLayout&~ResTable_config::MASK_LAYOUTDIR)
                | ResTable_config::LAYOUTDIR_LTR;
        return true;
    } else if (strcmp(name, "ldrtl") == 0) {
        if (out) out->screenLayout =
                (out->screenLayout&~ResTable_config::MASK_LAYOUTDIR)
                | ResTable_config::LAYOUTDIR_RTL;
        return true;
    }

    return false;
}

bool AaptGroupEntry::getScreenLayoutSizeName(const char* name,
                                     ResTable_config* out)
{
    if (strcmp(name, kWildcardName) == 0) {
        if (out) out->screenLayout =
                (out->screenLayout&~ResTable_config::MASK_SCREENSIZE)
                | ResTable_config::SCREENSIZE_ANY;
        return true;
    } else if (strcmp(name, "small") == 0) {
        if (out) out->screenLayout =
                (out->screenLayout&~ResTable_config::MASK_SCREENSIZE)
                | ResTable_config::SCREENSIZE_SMALL;
        return true;
    } else if (strcmp(name, "normal") == 0) {
        if (out) out->screenLayout =
                (out->screenLayout&~ResTable_config::MASK_SCREENSIZE)
                | ResTable_config::SCREENSIZE_NORMAL;
        return true;
    } else if (strcmp(name, "large") == 0) {
        if (out) out->screenLayout =
                (out->screenLayout&~ResTable_config::MASK_SCREENSIZE)
                | ResTable_config::SCREENSIZE_LARGE;
        return true;
    } else if (strcmp(name, "xlarge") == 0) {
        if (out) out->screenLayout =
                (out->screenLayout&~ResTable_config::MASK_SCREENSIZE)
                | ResTable_config::SCREENSIZE_XLARGE;
        return true;
    }

    return false;
}

bool AaptGroupEntry::getScreenLayoutLongName(const char* name,
                                     ResTable_config* out)
{
    if (strcmp(name, kWildcardName) == 0) {
        if (out) out->screenLayout =
                (out->screenLayout&~ResTable_config::MASK_SCREENLONG)
                | ResTable_config::SCREENLONG_ANY;
        return true;
    } else if (strcmp(name, "long") == 0) {
        if (out) out->screenLayout =
                (out->screenLayout&~ResTable_config::MASK_SCREENLONG)
                | ResTable_config::SCREENLONG_YES;
        return true;
    } else if (strcmp(name, "notlong") == 0) {
        if (out) out->screenLayout =
                (out->screenLayout&~ResTable_config::MASK_SCREENLONG)
                | ResTable_config::SCREENLONG_NO;
        return true;
    }

    return false;
}

bool AaptGroupEntry::getOrientationName(const char* name,
                                        ResTable_config* out)
{
    if (strcmp(name, kWildcardName) == 0) {
        if (out) out->orientation = out->ORIENTATION_ANY;
        return true;
    } else if (strcmp(name, "port") == 0) {
        if (out) out->orientation = out->ORIENTATION_PORT;
        return true;
    } else if (strcmp(name, "land") == 0) {
        if (out) out->orientation = out->ORIENTATION_LAND;
        return true;
    } else if (strcmp(name, "square") == 0) {
        if (out) out->orientation = out->ORIENTATION_SQUARE;
        return true;
    }

    return false;
}

bool AaptGroupEntry::getUiInvertedModeName(const char* name,
                                        ResTable_config* out)
{
    if (strcmp(name, kWildcardName) == 0) {
        if (out) out->uiInvertedMode = out->UI_INVERTED_MODE_ANY;
        return true;
    } else if (strcmp(name, "inverted") == 0) {
        if (out) out->uiInvertedMode = out->UI_INVERTED_MODE_YES;
        return true;
    } else if (strcmp(name, "notinverted") == 0) {
        if (out) out->uiInvertedMode = out->UI_INVERTED_MODE_NO;
        return true;
    }

    return false;
}

bool AaptGroupEntry::getUiModeTypeName(const char* name,
                                       ResTable_config* out)
{
    if (strcmp(name, kWildcardName) == 0) {
        if (out) out->uiMode =
                (out->uiMode&~ResTable_config::MASK_UI_MODE_TYPE)
                | ResTable_config::UI_MODE_TYPE_ANY;
        return true;
    } else if (strcmp(name, "desk") == 0) {
      if (out) out->uiMode =
              (out->uiMode&~ResTable_config::MASK_UI_MODE_TYPE)
              | ResTable_config::UI_MODE_TYPE_DESK;
        return true;
    } else if (strcmp(name, "car") == 0) {
      if (out) out->uiMode =
              (out->uiMode&~ResTable_config::MASK_UI_MODE_TYPE)
              | ResTable_config::UI_MODE_TYPE_CAR;
        return true;
    } else if (strcmp(name, "television") == 0) {
      if (out) out->uiMode =
              (out->uiMode&~ResTable_config::MASK_UI_MODE_TYPE)
              | ResTable_config::UI_MODE_TYPE_TELEVISION;
        return true;
    } else if (strcmp(name, "appliance") == 0) {
      if (out) out->uiMode =
              (out->uiMode&~ResTable_config::MASK_UI_MODE_TYPE)
              | ResTable_config::UI_MODE_TYPE_APPLIANCE;
        return true;
    }

    return false;
}

bool AaptGroupEntry::getUiModeNightName(const char* name,
                                          ResTable_config* out)
{
    if (strcmp(name, kWildcardName) == 0) {
        if (out) out->uiMode =
                (out->uiMode&~ResTable_config::MASK_UI_MODE_NIGHT)
                | ResTable_config::UI_MODE_NIGHT_ANY;
        return true;
    } else if (strcmp(name, "night") == 0) {
        if (out) out->uiMode =
                (out->uiMode&~ResTable_config::MASK_UI_MODE_NIGHT)
                | ResTable_config::UI_MODE_NIGHT_YES;
        return true;
    } else if (strcmp(name, "notnight") == 0) {
      if (out) out->uiMode =
              (out->uiMode&~ResTable_config::MASK_UI_MODE_NIGHT)
              | ResTable_config::UI_MODE_NIGHT_NO;
        return true;
    }

    return false;
}

bool AaptGroupEntry::getDensityName(const char* name,
                                    ResTable_config* out)
{
    if (strcmp(name, kWildcardName) == 0) {
        if (out) out->density = ResTable_config::DENSITY_DEFAULT;
        return true;
    }
    
    if (strcmp(name, "nodpi") == 0) {
        if (out) out->density = ResTable_config::DENSITY_NONE;
        return true;
    }
    
    if (strcmp(name, "ldpi") == 0) {
        if (out) out->density = ResTable_config::DENSITY_LOW;
        return true;
    }
    
    if (strcmp(name, "mdpi") == 0) {
        if (out) out->density = ResTable_config::DENSITY_MEDIUM;
        return true;
    }
    
    if (strcmp(name, "tvdpi") == 0) {
        if (out) out->density = ResTable_config::DENSITY_TV;
        return true;
    }
    
    if (strcmp(name, "hdpi") == 0) {
        if (out) out->density = ResTable_config::DENSITY_HIGH;
        return true;
    }

    if (strcmp(name, "xhdpi") == 0) {
        if (out) out->density = ResTable_config::DENSITY_XHIGH;
        return true;
    }

    if (strcmp(name, "xxhdpi") == 0) {
        if (out) out->density = ResTable_config::DENSITY_XXHIGH;
        return true;
    }

    char* c = (char*)name;
    while (*c >= '0' && *c <= '9') {
        c++;
    }

    // check that we have 'dpi' after the last digit.
    if (toupper(c[0]) != 'D' ||
            toupper(c[1]) != 'P' ||
            toupper(c[2]) != 'I' ||
            c[3] != 0) {
        return false;
    }

    // temporarily replace the first letter with \0 to
    // use atoi.
    char tmp = c[0];
    c[0] = '\0';

    int d = atoi(name);
    c[0] = tmp;

    if (d != 0) {
        if (out) out->density = d;
        return true;
    }

    return false;
}

bool AaptGroupEntry::getTouchscreenName(const char* name,
                                        ResTable_config* out)
{
    if (strcmp(name, kWildcardName) == 0) {
        if (out) out->touchscreen = out->TOUCHSCREEN_ANY;
        return true;
    } else if (strcmp(name, "notouch") == 0) {
        if (out) out->touchscreen = out->TOUCHSCREEN_NOTOUCH;
        return true;
    } else if (strcmp(name, "stylus") == 0) {
        if (out) out->touchscreen = out->TOUCHSCREEN_STYLUS;
        return true;
    } else if (strcmp(name, "finger") == 0) {
        if (out) out->touchscreen = out->TOUCHSCREEN_FINGER;
        return true;
    }

    return false;
}

bool AaptGroupEntry::getKeysHiddenName(const char* name,
                                       ResTable_config* out)
{
    uint8_t mask = 0;
    uint8_t value = 0;
    if (strcmp(name, kWildcardName) == 0) {
        mask = ResTable_config::MASK_KEYSHIDDEN;
        value = ResTable_config::KEYSHIDDEN_ANY;
    } else if (strcmp(name, "keysexposed") == 0) {
        mask = ResTable_config::MASK_KEYSHIDDEN;
        value = ResTable_config::KEYSHIDDEN_NO;
    } else if (strcmp(name, "keyshidden") == 0) {
        mask = ResTable_config::MASK_KEYSHIDDEN;
        value = ResTable_config::KEYSHIDDEN_YES;
    } else if (strcmp(name, "keyssoft") == 0) {
        mask = ResTable_config::MASK_KEYSHIDDEN;
        value = ResTable_config::KEYSHIDDEN_SOFT;
    }

    if (mask != 0) {
        if (out) out->inputFlags = (out->inputFlags&~mask) | value;
        return true;
    }

    return false;
}

bool AaptGroupEntry::getKeyboardName(const char* name,
                                        ResTable_config* out)
{
    if (strcmp(name, kWildcardName) == 0) {
        if (out) out->keyboard = out->KEYBOARD_ANY;
        return true;
    } else if (strcmp(name, "nokeys") == 0) {
        if (out) out->keyboard = out->KEYBOARD_NOKEYS;
        return true;
    } else if (strcmp(name, "qwerty") == 0) {
        if (out) out->keyboard = out->KEYBOARD_QWERTY;
        return true;
    } else if (strcmp(name, "12key") == 0) {
        if (out) out->keyboard = out->KEYBOARD_12KEY;
        return true;
    }

    return false;
}

bool AaptGroupEntry::getNavHiddenName(const char* name,
                                       ResTable_config* out)
{
    uint8_t mask = 0;
    uint8_t value = 0;
    if (strcmp(name, kWildcardName) == 0) {
        mask = ResTable_config::MASK_NAVHIDDEN;
        value = ResTable_config::NAVHIDDEN_ANY;
    } else if (strcmp(name, "navexposed") == 0) {
        mask = ResTable_config::MASK_NAVHIDDEN;
        value = ResTable_config::NAVHIDDEN_NO;
    } else if (strcmp(name, "navhidden") == 0) {
        mask = ResTable_config::MASK_NAVHIDDEN;
        value = ResTable_config::NAVHIDDEN_YES;
    }

    if (mask != 0) {
        if (out) out->inputFlags = (out->inputFlags&~mask) | value;
        return true;
    }

    return false;
}

bool AaptGroupEntry::getNavigationName(const char* name,
                                     ResTable_config* out)
{
    if (strcmp(name, kWildcardName) == 0) {
        if (out) out->navigation = out->NAVIGATION_ANY;
        return true;
    } else if (strcmp(name, "nonav") == 0) {
        if (out) out->navigation = out->NAVIGATION_NONAV;
        return true;
    } else if (strcmp(name, "dpad") == 0) {
        if (out) out->navigation = out->NAVIGATION_DPAD;
        return true;
    } else if (strcmp(name, "trackball") == 0) {
        if (out) out->navigation = out->NAVIGATION_TRACKBALL;
        return true;
    } else if (strcmp(name, "wheel") == 0) {
        if (out) out->navigation = out->NAVIGATION_WHEEL;
        return true;
    }

    return false;
}

bool AaptGroupEntry::getScreenSizeName(const char* name, ResTable_config* out)
{
    if (strcmp(name, kWildcardName) == 0) {
        if (out) {
            out->screenWidth = out->SCREENWIDTH_ANY;
            out->screenHeight = out->SCREENHEIGHT_ANY;
        }
        return true;
    }

    const char* x = name;
    while (*x >= '0' && *x <= '9') x++;
    if (x == name || *x != 'x') return false;
    String8 xName(name, x-name);
    x++;

    const char* y = x;
    while (*y >= '0' && *y <= '9') y++;
    if (y == name || *y != 0) return false;
    String8 yName(x, y-x);

    uint16_t w = (uint16_t)atoi(xName.string());
    uint16_t h = (uint16_t)atoi(yName.string());
    if (w < h) {
        return false;
    }

    if (out) {
        out->screenWidth = w;
        out->screenHeight = h;
    }

    return true;
}

bool AaptGroupEntry::getSmallestScreenWidthDpName(const char* name, ResTable_config* out)
{
    if (strcmp(name, kWildcardName) == 0) {
        if (out) {
            out->smallestScreenWidthDp = out->SCREENWIDTH_ANY;
        }
        return true;
    }

    if (*name != 's') return false;
    name++;
    if (*name != 'w') return false;
    name++;
    const char* x = name;
    while (*x >= '0' && *x <= '9') x++;
    if (x == name || x[0] != 'd' || x[1] != 'p' || x[2] != 0) return false;
    String8 xName(name, x-name);

    if (out) {
        out->smallestScreenWidthDp = (uint16_t)atoi(xName.string());
    }

    return true;
}

bool AaptGroupEntry::getScreenWidthDpName(const char* name, ResTable_config* out)
{
    if (strcmp(name, kWildcardName) == 0) {
        if (out) {
            out->screenWidthDp = out->SCREENWIDTH_ANY;
        }
        return true;
    }

    if (*name != 'w') return false;
    name++;
    const char* x = name;
    while (*x >= '0' && *x <= '9') x++;
    if (x == name || x[0] != 'd' || x[1] != 'p' || x[2] != 0) return false;
    String8 xName(name, x-name);

    if (out) {
        out->screenWidthDp = (uint16_t)atoi(xName.string());
    }

    return true;
}

bool AaptGroupEntry::getScreenHeightDpName(const char* name, ResTable_config* out)
{
    if (strcmp(name, kWildcardName) == 0) {
        if (out) {
            out->screenHeightDp = out->SCREENWIDTH_ANY;
        }
        return true;
    }

    if (*name != 'h') return false;
    name++;
    const char* x = name;
    while (*x >= '0' && *x <= '9') x++;
    if (x == name || x[0] != 'd' || x[1] != 'p' || x[2] != 0) return false;
    String8 xName(name, x-name);

    if (out) {
        out->screenHeightDp = (uint16_t)atoi(xName.string());
    }

    return true;
}

bool AaptGroupEntry::getVersionName(const char* name, ResTable_config* out)
{
    if (strcmp(name, kWildcardName) == 0) {
        if (out) {
            out->sdkVersion = out->SDKVERSION_ANY;
            out->minorVersion = out->MINORVERSION_ANY;
        }
        return true;
    }

    if (*name != 'v') {
        return false;
    }

    name++;
    const char* s = name;
    while (*s >= '0' && *s <= '9') s++;
    if (s == name || *s != 0) return false;
    String8 sdkName(name, s-name);

    if (out) {
        out->sdkVersion = (uint16_t)atoi(sdkName.string());
        out->minorVersion = 0;
    }

    return true;
}

int AaptGroupEntry::compare(const AaptGroupEntry& o) const
{
    int v = mcc.compare(o.mcc);
    if (v =

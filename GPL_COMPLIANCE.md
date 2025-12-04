# GPL Compliance Checklist for Zodiac Sidereal Distribution

## Legal Background

Your application is licensed under **GPL v3**, which requires you to:
1. Distribute the license text with your software
2. Make source code available to recipients
3. Allow recipients to redistribute and modify

The Swiss Ephemeris library you use is dual-licensed (GPL v2+ or commercial).
Since you're using it under GPL, your entire application must also be GPL.

## Compliance Checklist

### ✅ Already Completed

- [x] Application source code is on public GitHub repository
- [x] LICENSE file (GPL v3) is in project root
- [x] Swiss Ephemeris LICENSE file is included (swe/LICENSE)
- [x] NSIS installer displays GPL license during installation
- [x] License text (license.txt) is included in installation directory

### 📋 Required Actions Before Distribution

- [ ] **Add source code reference to installer**
  - The README_FOR_USERS.txt already includes GitHub URL
  - This will be installed with the application
  - ✓ Satisfies GPL requirement for source code availability

- [ ] **Verify license display during installation**
  - Test that GPL license appears in installer wizard
  - User must see it before accepting

- [ ] **Include warranty disclaimer**
  - Already in license.txt
  - Also mentioned in README_FOR_USERS.txt
  - ✓ Satisfies GPL section 15-16 requirements

### 📝 Recommended (Best Practices)

- [ ] **Add "About" dialog in application**
  - Show version number
  - Display "This is free software licensed under GPL v3"
  - Include link to GitHub repository
  - Include Swiss Ephemeris attribution

- [ ] **Create GitHub Release**
  - Tag version (e.g., v0.8.1)
  - Attach installer to release
  - Write release notes
  - Source code is automatically included by GitHub

- [ ] **Add COPYRIGHT or AUTHORS file**
  - List copyright holders
  - Add Swiss Ephemeris attribution
  - Mention other dependencies (Qt, Boost, etc.)

## GPL v3 Key Requirements

### 1. License Notice ✅
**Requirement:** Include copy of GPL with program
**Status:** COMPLIANT
- license.txt in installer
- LICENSE in source repository

### 2. Source Code Availability ✅
**Requirement:** Make source code available to recipients
**Status:** COMPLIANT
- Public GitHub repository
- URL provided in README_FOR_USERS.txt
- Users can download source at any time

### 3. License Preservation ✅
**Requirement:** Preserve license notices
**Status:** COMPLIANT
- All source files maintain their copyright headers
- Swiss Ephemeris license preserved in swe/LICENSE

### 4. Modification Rights ✅
**Requirement:** Allow users to modify and redistribute
**Status:** COMPLIANT
- Source code is available
- GPL license explicitly grants these rights
- No technical restrictions preventing modification

### 5. No Additional Restrictions ✅
**Requirement:** Cannot add restrictions beyond GPL
**Status:** COMPLIANT
- No DRM or technological restrictions
- No contractual restrictions
- Users receive same rights you received

### 6. Warranty Disclaimer ✅
**Requirement:** Include "no warranty" notice
**Status:** COMPLIANT
- GPL text includes warranty disclaimer
- README_FOR_USERS.txt emphasizes NO WARRANTY

## Distribution Scenarios

### Scenario 1: Direct Download (Friends)
**Compliant approach:**
1. Provide installer file
2. Installer displays GPL and installs README with GitHub URL
3. Users can get source from GitHub
✅ COMPLIANT

### Scenario 2: Email/File Sharing
**Compliant approach:**
1. Send installer file
2. Include brief note: "This is GPL software. Source: [GitHub URL]"
✅ COMPLIANT

### Scenario 3: Website Distribution
**Recommended approach:**
1. Host installer on GitHub Releases
2. Link to release page
3. Release page automatically includes source code download
✅ COMPLIANT (and best practice)

### Scenario 4: Physical Media (USB, CD)
**Required approach:**
1. Include installer on media
2. Include source code OR written offer for source
3. Written offer must be valid for 3 years
⚠️ If doing this, consider including source on media

## Common Questions

### Q: Do I need to include source code ON the installer?
**A:** No. You can provide a link to where source can be downloaded.
GitHub repository link in README_FOR_USERS.txt satisfies this.

### Q: Can friends redistribute to other friends?
**A:** Yes! That's the point of GPL. They receive the same rights you have.
They must also comply with GPL (provide source, preserve license, etc.)

### Q: What if I modify the code?
**A:** Modified versions must also be GPL v3 (or later). You must:
- Preserve original copyright notices
- Add your own copyright notice for your changes
- Make your modified source available
- Mark changes as different from original

### Q: Can I charge money for this software?
**A:** Yes, GPL allows charging for distribution. However:
- You cannot restrict recipients from sharing it freely
- You must still provide source code
- Recipients can redistribute for free if they want

### Q: Do I need permission from Swiss Ephemeris authors?
**A:** No. They licensed it under GPL v2+, which you're complying with.
Just preserve their copyright notices and license file.

### Q: What about Qt libraries?
**A:** Qt is LGPL, which allows linking from GPL software. You're fine.
The windeployqt tool handles Qt's license requirements automatically.

### Q: What about Boost libraries?
**A:** Boost uses the Boost Software License, which is extremely permissive.
- ✅ Compatible with GPL (can be combined freely)
- ✅ No attribution required (though it's nice to include)
- ✅ Only need to distribute the headers you use (header-only library)
- ✅ No runtime DLLs needed - Boost Math is header-only!
You don't need to do anything special for Boost.

## GPL v3 vs v2 (Swiss Ephemeris)

Swiss Ephemeris is GPL v2 "or later"
Your application is GPL v3

**Compatibility:** ✅ COMPATIBLE
- GPL v3 was designed to be compatible with "v2 or later"
- You can combine them in one program
- The combination must be distributed under GPL v3

## What Could Go Wrong (Violations)

### ❌ License Violations (Don't do these!)

1. **Remove GPL license text**
   - Keep license.txt in installer
   - Keep LICENSE in source repo

2. **Don't provide source code access**
   - Must include GitHub URL or provide source directly
   - README_FOR_USERS.txt already handles this

3. **Add proprietary restrictions**
   - Can't prevent modification
   - Can't prevent redistribution
   - Can't add DRM

4. **Claim different license**
   - Can't say it's "proprietary" or "all rights reserved"
   - Must clearly state GPL v3

5. **Remove copyright notices**
   - Must preserve all copyright statements
   - Including Swiss Ephemeris copyright

## Final Checklist Before First Distribution

Before you give the installer to anyone:

- [ ] Test installer on clean machine
- [ ] Verify license.txt is installed
- [ ] Verify README_FOR_USERS.txt is installed
- [ ] Verify README includes GitHub URL
- [ ] Confirm GitHub repository is public
- [ ] Verify Swiss Ephemeris license is in swe/ directory after install
- [ ] Test that application actually works after installation
- [ ] Confirm uninstaller works properly

## Resources

- GPL v3 Full Text: https://www.gnu.org/licenses/gpl-3.0.html
- GPL v3 FAQ: https://www.gnu.org/licenses/gpl-faq.html
- GPL Compliance Guide: https://www.softwarefreedom.org/resources/
- Swiss Ephemeris License: https://www.astro.com/swisseph/swephinfo_e.htm

## Summary

**You are currently GPL compliant** because:
1. ✅ Source code is publicly available (GitHub)
2. ✅ License files are included
3. ✅ Installer references source location
4. ✅ No additional restrictions imposed
5. ✅ Swiss Ephemeris license preserved

**To stay compliant:**
- Keep GitHub repository public
- Don't remove license files
- Don't add restrictions
- Make sure README_FOR_USERS.txt stays in installer

That's it! The GPL is designed to ensure freedom, not to be burdensome.
As long as you share the source and don't restrict users' rights, you're good!

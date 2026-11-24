## Plan: Local Lookup And Timezone Path

Add a third in-process geography lookup provider backed by the bundled GeoNames city data, and extend that same local data path to resolve timezone IDs into date-specific UTC offsets with Qt so location lookup and timezone lookup can both work without web API calls.

**Steps**
1. Keep the existing GeoSearchWidget UI and popup contract in place, and add a provider seam around GeoSuggestCompletion in [fileeditor/src/geosearch.h](c:/users/jbaud/GitHub/joshb-work-sidereal/fileeditor/src/geosearch.h) and [fileeditor/src/geosearch.cpp](c:/users/jbaud/GitHub/joshb-work-sidereal/fileeditor/src/geosearch.cpp). Reuse the current source-switch pattern that already distinguishes Google and Yandex, and extend it to support a third local/offline source.
2. Promote the city database in [astroprocessor/src/citydb.h](c:/users/jbaud/GitHub/joshb-work-sidereal/astroprocessor/src/citydb.h) and [astroprocessor/src/citydb.cpp](c:/users/jbaud/GitHub/joshb-work-sidereal/astroprocessor/src/citydb.cpp) from paran display support to general geocoding support by adding name-based query APIs. The current API is latitude-centric for paran output only, so this step must add a search path keyed by city name or normalized display name.
3. Expand `CityRec` and the loader so the local DB retains the GeoNames timezone ID already present in [bin/data/cities15000.txt](c:/users/jbaud/GitHub/joshb-work-sidereal/bin/data/cities15000.txt) and documented in [bin/data/geonames-readme.txt](c:/users/jbaud/GitHub/joshb-work-sidereal/bin/data/geonames-readme.txt#L61). Today the loader parses only name, country, latitude, longitude, and population, so the timezone information is lost before the app can use it.
4. Introduce a small local timezone-resolution helper in astroprocessor or fileeditor that takes a selected city record plus the relevant chart date/time and returns the correct offset in hours. Use Qt timezone facilities with the stored IANA zone ID to compute date-sensitive offsets, rather than persisting hard-coded raw offsets. This keeps DST behavior tied to the chart date.
5. Route the file editor’s location-change timezone update through that local helper first. The narrow integration point is `AstroFileEditor::updateTimezone` in [fileeditor/src/fileeditor.cpp](c:/users/jbaud/GitHub/joshb-work-sidereal/fileeditor/src/fileeditor.cpp#L334), which already owns the post-location-change timezone update and respects timezone locking and non-zone time modes.
6. Route the transits view’s location-change timezone update through the same local helper. The narrow integration point is the existing timezone-fetch block in [details/src/transits.cpp](c:/users/jbaud/GitHub/joshb-work-sidereal/details/src/transits.cpp#L3040), which currently stores only a numeric offset in the events model, `file(0)` transit state, and `transitsAF()`.
7. Decide how to handle unresolved local lookups and ambiguous cities. Recommended behavior: if the local provider yields a selected city, use its timezone ID directly; if the user enters raw coordinates manually or no city match is associated, either keep the existing manual timezone entry behavior in the editor and current transit timezone, or optionally fall back to the existing Google timezone path.
8. Expose the local provider in the source selector UI as a visible third option rather than silently overloading Google. That keeps behavior legible and lets users choose between offline/local and web-backed suggestions.
9. Preserve current scope boundaries around timezone persistence. The existing model stores numeric timezone offsets on `AstroFile` and per-tab transit state in [astroprocessor/src/astro-gui.h](c:/users/jbaud/GitHub/joshb-work-sidereal/astroprocessor/src/astro-gui.h#L292); the first iteration should keep that storage model and recompute offsets at lookup time rather than changing persistence to store IANA IDs everywhere.
10. Verify both user flows manually: file editor chart entry and transits location changes. Confirm the local provider can populate coordinates, compute timezone offsets for the relevant date, honor timezone locks, and avoid unexpected behavior in LMT or LAT modes.

**Relevant files**
- [fileeditor/src/geosearch.h](c:/users/jbaud/GitHub/joshb-work-sidereal/fileeditor/src/geosearch.h) — source enum and GeoSearchBox/GeoSearchWidget provider-selection surface.
- [fileeditor/src/geosearch.cpp](c:/users/jbaud/GitHub/joshb-work-sidereal/fileeditor/src/geosearch.cpp) — current Google/Yandex flow, popup contract, and source-selector actions to extend with a local provider.
- [astroprocessor/src/citydb.h](c:/users/jbaud/GitHub/joshb-work-sidereal/astroprocessor/src/citydb.h) — `CityRec` schema and DB query surface to extend with timezone ID and name search.
- [astroprocessor/src/citydb.cpp](c:/users/jbaud/GitHub/joshb-work-sidereal/astroprocessor/src/citydb.cpp) — GeoNames loader and query engine; needs to retain the timezone field from the raw data and expose search by name.
- [bin/data/cities15000.txt](c:/users/jbaud/GitHub/joshb-work-sidereal/bin/data/cities15000.txt) — bundled GeoNames data; contains per-city timezone IDs that can drive offline timezone resolution.
- [fileeditor/src/fileeditor.cpp](c:/users/jbaud/GitHub/joshb-work-sidereal/fileeditor/src/fileeditor.cpp#L334) — `AstroFileEditor::updateTimezone` and the timezone-lock flow for chart editing.
- [details/src/transits.cpp](c:/users/jbaud/GitHub/joshb-work-sidereal/details/src/transits.cpp#L3040) — transit location change handler and current Google timezone request path.
- [astroprocessor/src/astro-gui.h](c:/users/jbaud/GitHub/joshb-work-sidereal/astroprocessor/src/astro-gui.h#L292) — current persistence model for file timezone and per-tab transit timezone offsets.

**Verification**
1. In the file editor, select a city via the local provider and confirm coordinates populate and the timezone spinbox updates to the expected offset for the chart date.
2. Repeat with a DST-sensitive city on dates in and out of DST and confirm the computed offsets differ when expected.
3. In the file editor, confirm a locked timezone is not overwritten by a location change and that LMT/LAT modes still bypass timezone lookup.
4. In transits, select a city via the local provider and confirm the events model, `file(0)` transit state, and `transitsAF()` all receive the expected offset without a web call.
5. Test raw coordinate entry with no associated city selection and confirm the chosen fallback behavior is clear and consistent.
6. Test with API keys absent and network disconnected to confirm city suggestion and timezone computation still work when a local city match exists.

**Decisions**
- Included: offline place-name lookup and offline timezone resolution for city selections backed by the bundled GeoNames city extract.
- Recommended reuse point: compute timezone offsets from the stored IANA timezone ID using Qt at lookup time, instead of storing fixed raw offsets in the DB.
- Recommended persistence boundary: keep persisting numeric offsets on `AstroFile` and transit tab state in the first iteration, rather than expanding persistence to store timezone IDs everywhere.
- Constraint: the bundled local DB is cities 15k+, so smaller towns may not be found locally.
- Constraint: manual coordinate entry has no local timezone polygon dataset behind it, so city-based timezone resolution is reliable, coordinate-only timezone resolution is not.

**Further Considerations**
1. For coordinate-only input, choose explicitly between keeping manual timezone entry as the offline behavior or preserving Google timezone as an optional fallback when available.
2. If you later want true coordinate-based offline timezone lookup, that is a separate dataset and indexing problem; the current city DB is sufficient for city-selected lookups but not for arbitrary map points.
3. If name-search ambiguity becomes a UX problem, add country/region text into the popup description and prefer population-ranked ordering before considering broader geocoder refactors.

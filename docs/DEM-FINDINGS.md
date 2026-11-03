# Elevation source findings

Brief 7.4 asks for a spike of the Geoscience Australia 5 m LiDAR DEM (`DEM_LiDAR_5m_2025` MapServer, ArcGIS `identify`) over Kings Park, the Swan River foreshore and the Perth Hills, and the request rate that works.

## Status: not run (3 November 2026)

The cloud build environment's network policy refused both `services.ga.gov.au` and `api.opentopodata.org` (proxy CONNECT 403), so nothing was measured. No results are invented here.

What exists instead:

- `android/core` implements both providers (`GaProvider`: one `identify` request per sample point, at most 4 at a time, every sample cached by 1e-4 degree cell; `OpenTopoProvider`: 100 points per request, at most one request a second) and the fallback chain (GA, then Open Topo Data, then the GPX file's own elevations, labelled "from file"). They are tested against a mock server using the response shapes in the ArcGIS REST and Open Topo Data documentation. Whether the GA service answers in that shape (an `attributes."Pixel Value"` per result) is **Unverified**.
- `tools/dem_spike/dem_spike.py` (Python standard library only) runs the spike: eight Perth points through both services and a 40-request rate test at 4 concurrent requests. To run it, either let this environment reach those two hosts (environment settings, Network access, Allowed domains), or run it on a PC: `python3 tools/dem_spike/dem_spike.py > dem-spike.md`, then paste the output below.

## Results

(none yet)

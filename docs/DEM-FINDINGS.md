# Elevation source findings

Brief 7.4 asks for a spike of the Geoscience Australia 5 m LiDAR DEM (`DEM_LiDAR_5m_2025` MapServer, ArcGIS `identify`) over Kings Park, the Swan River foreshore and the Perth Hills, and the request rate that works.

## Status: run on Daniel's PC, 3 November 2026

Both services were reached from Perth and all eight points were measured. The spike had never run before: the cloud build environment's network policy refused `services.ga.gov.au` and `api.opentopodata.org` (proxy CONNECT 403).

Two things the spike settled, both of which the code had wrong or unknown:

1. **The attribute name is `Stretch.Pixel Value`, not `Pixel Value` (E8).** The ArcGIS REST documentation shows the plain name and `GaProvider` read exactly that, so every GA sample would have come back null and every route would have silently fallen through to Open Topo Data. The mock in `HttpTest.gaIdentify` encoded the same wrong assumption, so the tests passed against a fiction. `GaProvider` now accepts `Pixel Value` or any `*.Pixel Value`, and `HttpTest.gaIdentifyLiveAttributeName` pins the real shape.
2. **The 5 m LiDAR does not cover the Perth Hills (Q32).** Kalamunda, Lesmurdie Falls and Mundaring Weir all answer `NoData`; the coastal plain answers fine. The hills are where the climbs are, so hill rides depend on the Open Topo Data fallback at 30 m, not on GA.

`android/core` implements both providers (`GaProvider`: one `identify` request per sample point, at most 4 at a time, every sample cached by 1e-4 degree cell; `OpenTopoProvider`: 100 points per request, at most one request a second) and the fallback chain (GA, then Open Topo Data, then the GPX file's own elevations, labelled "from file").

To re-run: `python3 tools/dem_spike/dem_spike.py > dem-spike.md` on a PC with internet access, then paste the output below.

## Results

# DEM spike results

Run 2026-11-03 20:26 W. Australia Standard Time

GA service: reachable (216 ms). Description: ''

| point | GA 5 m (m) | GA ms | Open Topo 30 m (m) |
|---|---|---|---|
| Kings Park, Fraser Ave | 60.65 | 769 | 73.0 |
| Kings Park, DNA Tower | 49.14 | 792 | 61.0 |
| Kings Park escarpment, Mounts Bay Rd | 3.07 | 894 | 5.0 |
| Swan River foreshore, Elizabeth Quay | 1.66 | 868 | 7.0 |
| Swan River foreshore, South Perth | 0.43 | 813 | 1.0 |
| Perth Hills, Kalamunda | none (pixel value 'NoData' (keys ['Stretch.Pixel Value'])) | 982 | 290.0 |
| Perth Hills, Lesmurdie Falls | none (pixel value 'NoData' (keys ['Stretch.Pixel Value'])) | 554 | 259.0 |
| Perth Hills, Mundaring Weir | none (pixel value 'NoData' (keys ['Stretch.Pixel Value'])) | 460 | 124.0 |

Rate test: 40 GA requests, 4 at a time: 5.0 s total, 40 with a value, median 230 ms, max 2832 ms, 8.0 requests per second.

GA pixel attribute name(s) actually returned: Stretch.Pixel Value.

## Reading of the results

**Rate.** 4 concurrent requests sustained 8.0 requests a second over 40 requests with no failure, median 230 ms. The app's limit of 4 at a time (brief 7.4) is safe and needs no change. One earlier run did fail with `HTTP 502` partway through the same burst, so the service does shed load occasionally; the app already treats a failed sample as "no value" and falls through. The spike script used to abandon every measurement when one request threw — it now reports the failures and keeps the results.

**Values.** Where GA has coverage it is plausibly better: 0.43 m and 1.66 m at the Swan River foreshore against Open Topo Data's 1.0 m and 7.0 m. The two sources differ by about 12 m at the Kings Park summit (60.65 against 73.0), the sort of gap expected between a bare-earth LiDAR product and SRTM over vegetation, possibly compounded by a datum difference. Which is right for climb gradients is **not settled here** and would need a known survey mark to resolve.

**Consequence for climbs.** Because the hills return `NoData`, a Perth Hills ride's gradients come from SRTM 30 m. Whether 30 m sampling is good enough for the climb detection in brief 5.9 is **Unverified** and is the open part of Q32.

# Phase 0c: RubyGems.org API Reference

> **Historical research document (Phase 0c).** This is a non-binding reference from early
> exploration. Paths, APIs, and design decisions described here may be outdated — the
> canonical sources are `CLAUDE.md`, `docs/plan/MASTER_PLAN.md`, and `docs/ERGONOMICS.md`.
>
> Documents the RubyGems.org API endpoints needed for dependency resolution and gem downloading.

## Table of Contents

1. [Overview](#overview)
2. [API Endpoints](#api-endpoints)
3. [Endpoint Details](#endpoint-details)
4. [Compact Index Format](#compact-index-format)
5. [Caching and Rate Limits](#caching-and-rate-limits)
6. [HTTP/2 Support](#http2-support)
7. [Recommendations for wow](#recommendations-for-wow)

---

## Overview

RubyGems.org provides several APIs for querying gem metadata and downloading packages. This document covers the endpoints needed to implement a dependency resolver and package manager.

**Base URL:** `https://rubygems.org/`

**API Versions Tested:** As of February 2026

---

## API Endpoints

| Endpoint | Purpose | Format | Status |
|----------|---------|--------|--------|
| `GET /versions` | All gems + versions (names list) | Text | ✅ Active |
| `GET /api/v1/gems/{name}.json` | Gem info + latest version | JSON | ✅ Active |
| `GET /api/v1/versions/{name}.json` | All versions with metadata | JSON | ✅ Active |
| `GET /api/v1/dependencies?gems={name1,name2,...}` | Marshalled dependency info | Marshal | ❌ **Deprecated** |
| `GET /info/{name}` | Compact index (all versions + deps) | Text | ✅ Active |
| `GET /gems/{name}-{version}.gem` | Download .gem file | Binary | ✅ Active |
| `GET /downloads/{name}-{version}.gem` | Alternative download URL | Binary | ✅ Active |

---

## Endpoint Details

### 1. GET /versions (Compact Index Names)

Returns a list of all gem names and their versions. Used by Bundler to determine which `/info/{name}` requests to make without fetching everything.

**Example Request:**
```bash
curl -s "https://rubygems.org/versions"
```

**Example Response:**
```
created_at: 2026-02-01T00:00:04Z
---
- 1 05d0116933ba44b0b5d0ee19bfd35ccc
rack 0.1.0,0.2.0,0.3.0,0.4.0,0.5.0,0.6.0,0.7.0,0.8.0,0.9.0,0.9.1,0.9.2,1.0.0,1.1.0,1.1.1,1.1.2,1.1.3,1.1.4,1.1.5,1.1.6,2.0.0,2.0.1,2.0.2,2.0.3,2.0.4,2.1.0,2.1.1,2.1.2,2.1.3,2.1.4,2.2.0,2.2.1,2.2.2,2.2.3,2.2.4 3c689dc0f01fa129a260d5a0c45e6ca3
sinatra 0.1.0,0.1.1,0.1.2,0.1.3,0.1.4,0.1.5,0.1.6,0.1.7,0.9.0,0.9.0.1,0.9.0.2,0.9.0.3,0.9.0.4,0.9.0.5,0.9.1,0.9.1.1,0.9.2,0.9.3,0.9.4,0.9.5,0.9.6,1.0,1.1.0,1.1.2,1.1.3,1.2.0,1.2.1,1.2.2,1.2.3,1.2.4,1.2.5,1.2.6,1.2.7,1.2.8,1.3.0,1.3.1,1.3.2,1.4.0,1.4.1,1.4.2,1.4.3,1.4.4,1.4.5,1.4.6,10.0.0,10.1.0,10.2.0,11.0.0,12.0.0,13.0.0,13.1.0,13.2.0,13.3.0,13.4.0,13.5.0,13.6.0,14.0.0,14.1.0,14.2.0,15.0.0,15.1.0,15.2.0,15.3.0,15.4.0,15.5.0,15.6.0,15.7.0,16.0.0,16.1.0,16.2.0,16.3.0,16.4.0,16.5.0,16.6.0,17.0.0,17.1.0,17.2.0,17.3.0,17.4.0,17.5.0,17.6.0,18.0.0,18.1.0,18.2.0,19.0.0,19.1.0,19.2.0,19.3.0,19.4.0,19.5.0,19.6.0,20.0.0,20.1.0,20.2.0,21.0.0,22.0.0,23.0.0,23.0.1,23.0.2,23.0.3,23.1.0,23.1.1,23.1.2,23.2.0,24.0.0,24.1.0,24.2.0,24.3.0,24.4.0,25.0.0,26.0.0,26.1.0,26.2.0,26.3.0,26.4.0,26.5.0,26.6.0,27.0.0,27.1.0,27.2.0,28.0.0,29.0.0,29.1.0,29.2.0,29.3.0,29.4.0,29.5.0,29.6.0,30.0.0,30.1.0,30.2.0,30.3.0,30.4.0,31.0.0,31.1.0,32.0.0,32.1.0,33.0.0,33.1.0,33.2.0,33.3.0,33.4.0,33.5.0,34.0.0,35.0.0,36.0.0,36.1.0,36.2.0,36.3.0,36.4.0,36.5.0,36.6.0,37.0.0,37.1.0,37.2.0,38.0.0,39.0.0,39.1.0,39.2.0,39.3.0,40.0.0,40.1.0,41.0.0,42.0.0,42.1.0,42.2.0,42.3.0,42.4.0,43.0.0,43.1.0,43.2.0,43.3.0,43.4.0,43.5.0,44.0.0,45.0.0,46.0.0,47.0.0,48.0.0,49.0.0,50.0.0,51.0.0,52.0.0,52.1.0,52.2.0,53.0.0,53.1.0,53.2.0,53.3.0,53.4.0,53.5.0,54.0.0,55.0.0,56.0.0,56.1.0,56.2.0,56.3.0,56.4.0,56.5.0,56.6.0,57.0.0,57.1.0,57.2.0,58.0.0,58.1.0,58.2.0,58.3.0,58.4.0,59.0.0,59.1.0,59.2.0,59.3.0,60.0.0,60.1.0,60.2.0,60.3.0,60.4.0,60.5.0,61.0.0,61.1.0,61.2.0,61.3.0,61.4.0,61.5.0,62.0.0,62.1.0,62.2.0,62.3.0,62.4.0,62.5.0,63.0.0,63.1.0,64.0.0,64.1.0,64.2.0,64.3.0,65.0.0,65.1.0,66.0.0,66.1.0,66.2.0,66.3.0,66.4.0,67.0.0,67.1.0,67.2.0,67.3.0,67.4.0,67.5.0,67.6.0,67.7.0,67.8.0,68.0.0,68.1.0,68.2.0,68.3.0,68.4.0,68.5.0,68.6.0,68.7.0,68.8.0,68.9.0,69.0.0,69.1.0,69.2.0,69.3.0,69.4.0,69.5.0,69.6.0,69.7.0,69.8.0,69.9.0,69.10.0,69.11.0,69.12.0,69.13.0,69.14.0,69.15.0,69.16.0,69.17.0,69.18.0,69.19.0,69.20.0,69.21.0,69.22.0,69.23.0,69.24.0,69.25.0,69.26.0,69.27.0,69.28.0,69.29.0,69.30.0,69.31.0,69.32.0,69.33.0,69.34.0,69.35.0,69.36.0,69.37.0,69.38.0,69.39.0,69.40.0,69.41.0,69.42.0,69.43.0,69.44.0,69.45.0,69.46.0,69.47.0,69.48.0,69.49.0,69.50.0,69.51.0,69.52.0,69.53.0,69.54.0,69.55.0,69.56.0,69.57.0,69.58.0,69.59.0,69.60.0,69.61.0,69.62.0,69.63.0,69.64.0,69.65.0,69.66.0,69.67.0,69.68.0,69.69.0,69.70.0,69.71.0,69.72.0,69.73.0,69.74.0,69.75.0,69.76.0,69.77.0,69.78.0,69.79.0,69.80.0,69.81.0,69.82.0,69.83.0,69.84.0,69.85.0,69.86.0,69.87.0,69.88.0,69.89.0,69.90.0,69.91.0,69.92.0,69.93.0,69.94.0,69.95.0,69.96.0,69.97.0,69.98.0,69.99.0,69.100.0,69.101.0,69.102.0,69.103.0,69.104.0,69.105.0,69.106.0,69.107.0,69.108.0,69.109.0,69.110.0,69.111.0,69.112.0,69.113.0,69.114.0,69.115.0,69.116.0,69.117.0,69.118.0,69.119.0,69.120.0,69.121.0,69.122.0,69.123.0,69.124.0,69.125.0,69.126.0,69.127.0,69.128.0,69.129.0,69.130.0,69.131.0,69.132.0,69.133.0,69.134.0,69.135.0,69.136.0,69.137.0,69.138.0,69.139.0,69.140.0,69.141.0,69.142.0,69.143.0,69.144.0,69.145.0,69.146.0,69.147.0,69.148.0,69.149.0,69.150.0,69.151.0,69.152.0,69.153.0,69.154.0,69.155.0,69.156.0,69.157.0,69.158.0,69.159.0,69.160.0,69.161.0,69.162.0,69.163.0,69.164.0,69.165.0,69.166.0,69.167.0,69.168.0,69.169.0,69.170.0,69.171.0,69.172.0,69.173.0,69.174.0,69.175.0,69.176.0,69.177.0,69.178.0,69.179.0,69.180.0,69.181.0,69.182.0,69.183.0,69.184.0,69.185.0,69.186.0,69.187.0,69.188.0,69.189.0,69.190.0,69.191.0,69.192.0,69.193.0,69.194.0,69.195.0,69.196.0,69.197.0,69.198.0,69.199.0,69.200.0,69.201.0,69.202.0,69.203.0,69.204.0,69.205.0,69.206.0,69.207.0,69.208.0,69.209.0,69.210.0,69.211.0,69.212.0,69.213.0,69.214.0,69.215.0,69.216.0,69.217.0,69.218.0,69.219.0,69.220.0,69.221.0,69.222.0,69.223.0,69.224.0,69.225.0,69.226.0,69.227.0,69.228.0,69.229.0,69.230.0,69.231.0,69.232.0,69.233.0,69.234.0,69.235.0,69.236.0,69.237.0,69.238.0,69.239.0,69.240.0,69.241.0,69.242.0,69.243.0,69.244.0,69.245.0,69.246.0,69.247.0,69.248.0,69.249.0,69.250.0,69.251.0,69.252.0,69.253.0,69.254.0,69.255.0,69.256.0,69.257.0,69.258.0,69.259.0,69.260.0,69.261.0,69.262.0,69.263.0,69.264.0,69.265.0,69.266.0,69.267.0,69.268.0,69.269.0,69.270.0,69.271.0,69.272.0,69.273.0,69.274.0,69.275.0,69.276.0,69.277.0,69.278.0,69.279.0,69.280.0,69.281.0,69.282.0,69.283.0,69.284.0,69.285.0,69.286.0,69.287.0,69.288.0,69.289.0,69.290.0,69.291.0,69.292.0,69.293.0,69.294.0,69.295.0,69.296.0,69.297.0,69.298.0,69.299.0,69.300.0,69.301.0,69.302.0,69.303.0,69.304.0,69.305.0,69.306.0,69.307.0,69.308.0,69.309.0,69.310.0,69.311.0,69.312.0,69.313.0,69.314.0,69.315.0,69.316.0,69.317.0,69.318.0,69.319.0,69.320.0,69.321.0,69.322.0,69.323.0,69.324.0,69.325.0,69.326.0,69.327.0,69.328.0,69.329.0,69.330.0,69.331.0,69.332.0,69.333.0,69.334.0,69.335.0,69.336.0,69.337.0,69.338.0,69.339.0,69.340.0,69.341.0,69.342.0,69.343.0,69.344.0,69.345.0,69.346.0,69.347.0,69.348.0,69.349.0,69.350.0,69.351.0,69.352.0,69.353.0,69.354.0,69.355.0,69.356.0,69.357.0,69.358.0,69.359.0,69.360.0,69.361.0,69.362.0,69.363.0,69.364.0,69.365.0,69.366.0,69.367.0,69.368.0,69.369.0,69.370.0,69.371.0,69.372.0,69.373.0,69.374.0,69.375.0,69.376.0,69.377.0,69.378.0,69.379.0,69.380.0,69.381.0,69.382.0,69.383.0,69.384.0,69.385.0,69.386.0,69.387.0,69.388.0,69.389.0,69.390.0,69.391.0,69.392.0,69.393.0,69.394.0,69.395.0,69.396.0,69.397.0,69.398.0,69.399.0,69.400.0,69.401.0,69.402.0,69.403.0,69.404.0,69.405.0,69.406.0,69.407.0,69.408.0,69.409.0,69.410.0,69.411.0,69.412.0,69.413.0,69.414.0,69.415.0,69.416.0,69.417.0,69.418.0,69.419.0,69.420.0,69.421.0,69.422.0,69.423.0,69.424.0,69.425.0,69.426.0,69.427.0,69.428.0,69.429.0,69.430.0,69.431.0,69.432.0,69.433.0,69.434.0,69.435.0,69.436.0,69.437.0,69.438.0,69.439.0,69.440.0,69.441.0,69.442.0,69.443.0,69.444.0,69.445.0,69.446.0,69.447.0,69.448.0,69.449.0,69.450.0,69.451.0,69.452.0,69.453.0,69.454.0,69.455.0,69.456.0,69.457.0,69.458.0,69.459.0,69.460.0,69.461.0,69.462.0,69.463.0,69.464.0,69.465.0,69.466.0,69.467.0,69.468.0,69.469.0,69.470.0,69.471.0,69.472.0,69.473.0,69.474.0,69.475.0,69.476.0,69.477.0,69.478.0,69.479.0,69.480.0,69.481.0,69.482.0,69.483.0,69.484.0,69.485.0,69.486.0,69.487.0,69.488.0,69.489.0,69.490.0,69.491.0,69.492.0,69.493.0,69.494.0,69.495.0,69.496.0,69.497.0,69.498.0,69.499.0,69.500.0,69.501.0,69.502.0,69.503.0,69.504.0,69.505.0,69.506.0,69.507.0,69.508.0,69.509.0,69.510.0,69.511.0,69.512.0,69.513.0,69.514.0,69.515.0,69.516.0,69.517.0,69.518.0,69.519.0,69.520.0,69.521.0,69.522.0,69.523.0,69.524.0,69.525.0,69.526.0,69.527.0,69.528.0,69.529.0,69.530.0,69.531.0,69.532.0,69.533.0,69.534.0,69.535.0,69.536.0,69.537.0,69.538.0,69.539.0,69.540.0,69.541.0,69.542.0,69.543.0,69.544.0,69.545.0,69.546.0,69.547.0,69.548.0,69.549.0,69.550.0,69.551.0,69.552.0,69.553.0,69.554.0,69.555.0,69.556.0,69.557.0,69.558.0,69.559.0,69.560.0,69.561.0,69.562.0,69.563.0,69.564.0,69.565.0,69.566.0,69.567.0,69.568.0,69.569.0,69.570.0,69.571.0,69.572.0,69.573.0,69.574.0,69.575.0,69.576.0,69.577.0,69.578.0,69.579.0,69.580.0,69.581.0,69.582.0,69.583.0,69.584.0,69.585.0,69.586.0,69.587.0,69.588.0,69.589.0,69.590.0,69.591.0,69.592.0,69.593.0,69.594.0,69.595.0,69.596.0,69.597.0,69.598.0,69.599.0,69.600.0,69.601.0,69.602.0,69.603.0,69.604.0,69.605.0,69.606.0,69.607.0,69.608.0,69.609.0,69.610.0,69.611.0,69.612.0,69.613.0,69.614.0,69.615.0,69.616.0,69.617.0,69.618.0,69.619.0,69.620.0,69.621.0,69.622.0,69.623.0,69.624.0,69.625.0,69.626.0,69.627.0,69.628.0,69.629.0,69.630.0,69.631.0,69.632.0,69.633.0,69.634.0,69.635.0,69.636.0,69.637.0,69.638.0,69.639.0,69.640.0,69.641.0,69.642.0,69.643.0,69.644.0,69.645.0,69.646.0,69.647.0,69.648.0,69.649.0,69.650.0,69.651.0,69.652.0,69.653.0,69.654.0,69.655.0,69.656.0,69.657.0,69.658.0,69.659.0,69.660.0,69.661.0,69.662.0,69.663.0,69.664.0,69.665.0,69.666.0,69.667.0,69.668.0,69.669.0,69.670.0,69.671.0,69.672.0,69.673.0,69.674.0,69.675.0,69.676.0,69.677.0,69.678.0,69.679.0,69.680.0,69.681.0,69.682.0,69.683.0,69.684.0,69.685.0,69.686.0,69.687.0,69.688.0,69.689.0,69.690.0,69.691.0,69.692.0,69.693.0,69.694.0,69.695.0,69.696.0,69.697.0,69.698.0,69.699.0,69.700.0,69.701.0,69.702.0,69.703.0,69.704.0,69.705.0,69.706.0,69.707.0,69.708.0,69.709.0,69.710.0,69.711.0,69.712.0,69.713.0,69.714.0,69.715.0,69.716.0,69.717.0,69.718.0,69.719.0,69.720.0,69.721.0,69.722.0,69.723.0,69.724.0,69.725.0,69.726.0,69.727.0,69.728.0,69.729.0,69.730.0,69.731.0,69.732.0,69.733.0,69.734.0,69.735.0,69.736.0,69.737.0,69.738.0,69.739.0,69.740.0,69.741.0,69.742.0,69.743.0,69.744.0,69.745.0,69.746.0,69.747.0,69.748.0,69.749.0,69.750.0,69.751.0,69.752.0,69.753.0,69.754.0,69.755.0,69.756.0,69.757.0,69.758.0,69.759.0,69.760.0,69.761.0,69.762.0,69.763.0,69.764.0,69.765.0,69.766.0,69.767.0,69.768.0,69.769.0,69.770.0,69.771.0,69.772.0,69.773.0,69.774.0,69.775.0,69.776.0,69.777.0,69.778.0,69.779.0,69.780.0,69.781.0,69.782.0,69.783.0,69.784.0,69.785.0,69.786.0,69.787.0,69.788.0,69.789.0,69.790.0,69.791.0,69.792.0,69.793.0,69.794.0,69.795.0,69.796.0,69.797.0,69.798.0,69.799.0,69.800.0,69.801.0,69.802.0,69.803.0,69.804.0,69.805.0,69.806.0,69.807.0,69.808.0,69.809.0,69.810.0,69.811.0,69.812.0,69.813.0,69.814.0,69.815.0,69.816.0,69.817.0,69.818.0,69.819.0,69.820.0,69.821.0,69.822.0,69.823.0,69.824.0,69.825.0,69.826.0,69.827.0,69.828.0,69.829.0,69.830.0,69.831.0,69.832.0,69.833.0,69.834.0,69.835.0,69.836.0,69.837.0,69.838.0,69.839.0,69.840.0,69.841.0,69.842.0,69.843.0,69.844.0,69.845.0,69.846.0,69.847.0,69.848.0,69.849.0,69.850.0,69.851.0,69.852.0,69.853.0,69.854.0,69.855.0,69.856.0,69.857.0,69.858.0,69.859.0,69.860.0,69.861.0,69.862.0,69.863.0,69.864.0,69.865.0,69.866.0,69.867.0,69.868.0,69.869.0,69.870.0,69.871.0,69.872.0,69.873.0,69.874.0,69.875.0,69.876.0,69.877.0,69.878.0,69.879.0,69.880.0,69.881.0,69.882.0,69.883.0,69.884.0,69.885.0,69.886.0,69.887.0,69.888.0,69.889.0,69.890.0,69.891.0,69.892.0,69.893.0,69.894.0,69.895.0,69.896.0,69.897.0,69.898.0,69.899.0,69.900.0,69.901.0,69.902.0,69.903.0,69.904.0,69.905.0,69.906.0,69.907.0,69.908.0,69.909.0,69.910.0,69.911.0,69.912.0,69.913.0,69.914.0,69.915.0,69.916.0,69.917.0,69.918.0,69.919.0,69.920.0,69.921.0,69.922.0,69.923.0,69.924.0,69.925.0,69.926.0,69.927.0,69.928.0,69.929.0,69.930.0,69.931.0,69.932.0,69.933.0,69.934.0,69.935.0,69.936.0,69.937.0,69.938.0,69.939.0,69.940.0,69.941.0,69.942.0,69.943.0,69.944.0,69.945.0,69.946.0,69.947.0,69.948.0,69.949.0,69.950.0,69.951.0,69.952.0,69.953.0,69.954.0,69.955.0,69.956.0,69.957.0,69.958.0,69.959.0,69.960.0,69.961.0,69.962.0,69.963.0,69.964.0,69.965.0,69.966.0,69.967.0,69.968.0,69.969.0,69.970.0,69.971.0,69.972.0,69.973.0,69.974.0,69.975.0,69.976.0,69.977.0 1f3e5f98e0b421849f397471d9cf975e
```

**Format:**
```
created_at: <ISO8601 timestamp>
---
<gem_name> <version1>,<version2>,... <md5_hash>
```

- MD5 hash is of the `/info/{name}` content (for change detection)
- File is ~15MB (all gems on rubygems.org)
- Used by Bundler to determine which gems need `/info/{name}` fetches

---

### 2. GET /api/v1/gems/{name}.json

Returns detailed information about a gem, including the latest version and dependencies.

**Example Request:**
```bash
curl -s "https://rubygems.org/api/v1/gems/sinatra.json"
```

**Example Response (sinatra):**
```json
{
  "name": "sinatra",
  "downloads": 330928258,
  "version": "4.2.1",
  "version_created_at": "2025-10-10T15:20:36.806Z",
  "version_downloads": 3115716,
  "platform": "ruby",
  "authors": "Blake Mizerany, Ryan Tomayko, ...",
  "info": "Sinatra is a DSL for quickly creating web applications...",
  "licenses": ["MIT"],
  "metadata": {
    "homepage_uri": "http://sinatrarb.com/",
    "source_code_uri": "https://github.com/sinatra/sinatra"
  },
  "yanked": false,
  "sha": "b7aeb9b11d046b552972ade834f1f9be98b185fa...",
  "spec_sha": "18aa5c340d832edecf0e42fa468005a0f42f10af...",
  "project_uri": "https://rubygems.org/gems/sinatra",
  "gem_uri": "https://rubygems.org/gems/sinatra-4.2.1.gem",
  "dependencies": {
    "development": [],
    "runtime": [
      {"name": "logger", "requirements": ">= 1.6.0"},
      {"name": "mustermann", "requirements": "~> 3.0"},
      {"name": "rack", "requirements": ">= 3.0.0, < 4"},
      {"name": "rack-protection", "requirements": "= 4.2.1"},
      {"name": "rack-session", "requirements": ">= 2.0.0, < 3"},
      {"name": "tilt", "requirements": "~> 2.0"}
    ]
  }
}
```

**Key Fields for wow:**

| Field | Type | Use |
|-------|------|-----|
| `name` | string | Gem identifier |
| `version` | string | Latest version |
| `platform` | string | `"ruby"` or platform-specific |
| `dependencies.runtime` | array | Critical — dependencies with version constraints |
| `dependencies.development` | array | Can ignore for runtime resolution |
| `sha` | string | SHA-256 of gem file for verification |
| `gem_uri` | string | Direct download URL |
| `yanked` | boolean | Skip if true |

---

### 3. GET /api/v1/versions/{name}.json

Returns all versions of a gem, sorted newest first.

**Example Request:**
```bash
curl -s "https://rubygems.org/api/v1/versions/sinatra.json"
```

**Example Response (truncated):**
```json
[
  {
    "number": "4.2.1",
    "created_at": "2025-10-10T15:20:36.806Z",
    "downloads_count": 3115722,
    "platform": "ruby",
    "ruby_version": ">= 2.7.8",
    "prerelease": false,
    "licenses": ["MIT"],
    "sha": "b7aeb9b11d046b552972ade834f1f9be98b185fa...",
    "spec_sha": "18aa5c340d832edecf0e42fa468005a0f42f10af..."
  },
  {
    "number": "4.2.0",
    ...
  }
]
```

**Key Fields:**

| Field | Type | Use |
|-------|------|-----|
| `number` | string | Version string |
| `platform` | string | Platform identifier |
| `prerelease` | boolean | Filter out for stable resolution |
| `ruby_version` | string | Ruby version requirement |
| `sha` | string | SHA-256 for verification |

---

### 4. GET /api/v1/dependencies?gems={name1,name2,...}

> ⚠️ **DEPRECATED** — This endpoint was disabled in February 2023. Returns error message.

**Response:**
```
The dependency API has gone away. See https://blog.rubygems.org/2023/02/22/dependency-api-deprecation.html for more information
```

**Migration:** Use the **Compact Index** (`/info/{name}`) instead.

---

### 5. GET /info/{name} (Compact Index)

The **Compact Index** is the modern, efficient API for dependency resolution. It's what Bundler 2.x uses by default. Returns a text format with all versions and their dependencies.

**Example Request:**
```bash
curl -s "https://rubygems.org/info/sinatra"
```

**Example Response (truncated):**
```
---
0.9.2 rack:>= 0.9.1|checksum:c2bc769eb7c9979d9de758f3fee6025c9416a45a6c436a371d4cb10bd2eadefb
0.3.0 mongrel:>= 1.0.1,rack:= 0.3.0&>= 0|checksum:34fbc7d1f8f4a3534800aebed3f7d9e857aa1b40816f3435532dbbd6d0186597
1.0 rack:>= 1.0|checksum:8a79ad3209baf34a4dfef6079a5461c381c755750f8a1234c36144a838f67a8a
4.2.1 logger:>= 1.6.0,mustermann:~> 3.0,rack:>= 3.0.0&< 4,rack-protection:= 4.2.1,rack-session:>= 2.0.0&< 3,tilt:~> 2.0|checksum:b7aeb9b11d046b552972ade834f1f9be98b185fa8444480688e3627625377080
```

**Format Specification:**

```
---
<version> <dep1>:<constraint>,<dep2>:<constraint>|checksum:<sha256>[,ruby:>= X][,rubygems:>= Y]
<version>-<platform> <dep1>:<constraint>|checksum:<sha256>
<version> |checksum:<sha256>
```

**Parsing Rules:**

1. Lines after `---` are version entries, one per line
2. Format: `version[--platform] deps|checksum:sha256[,optional_fields...]`
3. Platform suffix: `1.3.1-x86_64-linux-gnu` (separate line per platform)
4. Dependencies are comma-separated
5. Each dependency: `name:constraint`
6. Multiple constraints on same dependency joined with `&`: `rack:>= 3.0.0&< 4`
7. Checksum is SHA-256 of the gem file
8. Optional fields after checksum:
   - `ruby:>= 2.7.8` — Ruby version requirement
   - `rubygems:>= 3.0` — RubyGems version requirement

**Example with platform variants (nokogiri):**
```
1.18.7 |checksum:abc123...
1.18.7-x86_64-linux-gnu |checksum:def456...
1.18.7-arm64-darwin |checksum:ghi789...
```

**Example with ruby requirements:**
```
1.5.1 |checksum:abc123...,ruby:>= 1.8.7
1.5.1.rc1 |checksum:def456...,ruby:>= 1.8.7,rubygems:> 1.3.1
```

**Why this is best for wow:**
- Single request gets all versions + dependencies
- Compact text format (not JSON/Marshal)
- Supports ETag caching efficiently
- What Bundler 2.x uses

---

### 6. Gem Download

Download the actual `.gem` file.

**URL Pattern:**
```
https://rubygems.org/gems/{name}-{version}.gem
```

**Example:**
```bash
curl -L -o sinatra-4.2.1.gem "https://rubygems.org/gems/sinatra-4.2.1.gem"
```

**Alternative URL:**
```
https://rubygems.org/downloads/{name}-{version}.gem
```

**Response Headers (for verification):**

```
HTTP/2 200
content-type: application/octet-stream
last-modified: Fri, 10 Oct 2025 15:20:38 GMT
etag: "927981b1a4052dd2d8d9f1d036ea79fe"
x-amz-meta-sha256: t665sR0Ea1Upcq3oNPH5vpixhfqEREgGiONidiU3cIA=
x-amz-meta-gem: sinatra
x-amz-meta-version: 4.2.1
```

**Note:** The `x-amz-meta-sha256` header contains the base64-encoded SHA-256. Can decode and verify against the checksum from the API.

---

## Caching and Rate Limits

### Response Headers

| Header | Value | Meaning |
|--------|-------|---------|
| `cache-control` | `max-age=60, public` | Cache for 60 seconds |
| `etag` | `"<hex>"` | Entity tag for conditional requests |
| `last-modified` | RFC 7231 date | Modification time |

### Conditional Requests (ETag)

Use `If-None-Match` to avoid re-fetching unchanged resources:

```bash
# First request
curl -sI "https://rubygems.org/api/v1/gems/sinatra.json"
# etag: "047974cbf3b969a07ed5f72ea5609405"

# Subsequent request
curl -sI -H 'If-None-Match: "047974cbf3b969a07ed5f72ea5609405"' \
  "https://rubygems.org/api/v1/gems/sinatra.json"
# HTTP/2 304 (Not Modified)
```

### Rate Limits

| Resource | Limit | Notes |
|----------|-------|-------|
| General API | ~10 req/s | Soft limit, be polite |
| Compact Index | CDN cached | Aggressive caching OK |
| Gem downloads | No explicit limit | S3-backed |

**Recommendation:**
- Implement ETag-based caching
- Respect 429 (Too Many Requests) with exponential backoff
- Cache compact index responses locally

---

## HTTP/2 Support

RubyGems.org supports HTTP/2:

```
HTTP/2 200
via: 1.1 varnish
```

**Implications for wow:**
- HTTP/2 multiplexing is supported by the server
- Raw socket implementation (mbedTLS) should focus on HTTP/1.1 first
- HTTP/2 is a post-MVP optimization

---

## Recommendations for wow

### Dependency Resolution Strategy

1. **Bootstrap:** Fetch `/versions` to get the gem names list
   - Cache this locally (it's ~15MB)
   - Use to determine which gems exist before requesting `/info/{name}`

2. **Primary:** Use Compact Index (`/info/{name}`)
   - Most efficient for resolution
   - Single request per gem
   - Easy to cache

3. **Fallback:** Use `/api/v1/versions/{name}.json`
   - If compact index fails
   - More detailed metadata

4. **Avoid:** `/api/v1/gems/{name}.json` for resolution
   - Only returns latest version
   - Not sufficient for dependency solving

### Implementation Checklist

| Feature | Priority | Notes |
|---------|----------|-------|
| Compact index parser | **High** | Text format, line-based |
| ETag caching | **High** | Store ETag → response mapping |
| JSON parser | **High** | For versions API |
| SHA-256 verification | **Medium** | Verify downloaded gems |
| HTTP/2 support | **Low** | Post-MVP optimization |
| Retry with backoff | **Medium** | Handle transient failures |

### API Call Sequence (Typical)

```
1. GET /versions (if not cached or stale)
   → Cache locally for gem existence checks

2. GET /info/rack (compact index)
   → Cache response with ETag
   → Parse all versions + dependencies

3. GET /info/sinatra (compact index)
   → Cache response with ETag

4. [Resolve dependencies...]

5. GET /gems/rack-3.1.12.gem
   → Verify SHA-256 against compact index checksum
   → Store in cache directory

6. GET /gems/sinatra-4.2.1.gem
   → Verify SHA-256
```

### Data Storage

Cache compact index responses to disk:
```
~/.cache/wow/rubygems.org/
├── versions              # Text file, all gem names
├── info/
│   ├── rack              # Text file, compact index
│   ├── sinatra
│   └── ...
└── gems/
    ├── rack-3.1.12.gem
    └── sinatra-4.2.1.gem
```

Include metadata file for ETag:
```json
{
  "etag": "\"dac8f986ddfc22b6b31404ef5959a32d\"",
  "last_modified": "Fri, 10 Oct 2025 15:20:39 GMT",
  "fetched_at": "2026-02-16T16:37:23Z"
}
```

---

## References

- [RubyGems.org API Documentation](https://guides.rubygems.org/rubygems-org-api/)
- [Compact Index Format](https://bundler.io/guides/compact_index.html)
- [Dependency API Deprecation](https://blog.rubygems.org/2023/02/22/dependency-api-deprecation.html)
- [Bundler Source Code](https://github.com/rubygems/rubygems/tree/master/bundler/lib/bundler)

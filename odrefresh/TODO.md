# TODO Work Items

## TODO (STOPSHIP until done)

1. Add a log file that tracks status of recent compilation.
2. Implement back off on trying compilation when previous attempt(s) failed.
3. Free space calculation and only attempting compilation if sufficient space.
4. Metrics for tracking issues:
   - Successful compilation of all artifacts.
   - Time limit exceeded (indicates a pathological issue, e.g. dex2oat bug, device driver bug, etc).
   - Insufficient space for compilation.
   - Compilation failure (boot extensions)
   - Compilation failure (system server)
   - Unexpected error (a setup or clean-up action failed).
5. Metrics recording for subprocess timeouts.
6. Decide and implement testing.

## DONE

1. <strike>Fix dexoptanalyzer so it can analyze boot extensions.</strike>
2. <strike>Parse apex-info-list.xml into an apex_info (to make version and location available).</strike>
3. <strike>Timeouts for pathological failures.</strike>

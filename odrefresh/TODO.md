# TODO Work Items

## TODO (STOPSHIP until done)

1. Implement back off on trying compilation when previous attempt(s) failed.

## DONE

<strike>

1. Fix dexoptanalyzer so it can analyze boot extensions.
2. Parse apex-info-list.xml into an apex_info (to make version and location available).
3. Timeouts for pathological failures.
4. Add a log file that tracks status of recent compilation.
5. Metrics for tracking issues:
   - Successful compilation of all artifacts.
   - Time limit exceeded (indicates a pathological issue, e.g. dex2oat bug, device driver bug, etc).
   - Insufficient space for compilation.
   - Compilation failure (boot extensions)
   - Compilation failure (system server)
   - Unexpected error (a setup or clean-up action failed).
6. Metrics recording for subprocess timeouts.
7. Free space calculation and only attempting compilation if sufficient space.

</strike>
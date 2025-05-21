REM Build NavMesh for all levels.
start /ABOVENORMAL recast -console levels\mp_lobby.gset 1
start /ABOVENORMAL recast -console levels\mp_rr_aqueduct.gset 1
start /ABOVENORMAL recast -console levels\mp_rr_arena_composite.gset 1
start /ABOVENORMAL recast -console levels\mp_rr_arena_empty.gset 1
start /ABOVENORMAL recast -console levels\mp_rr_arena_phase_runner.gset 1
start /ABOVENORMAL recast -console levels\mp_rr_canyonlands_64k_x_64k.gset 1
start /ABOVENORMAL recast -console levels\mp_rr_canyonlands_mu1.gset 1
start /ABOVENORMAL recast -console levels\mp_rr_canyonlands_mu1_night.gset 1
start /ABOVENORMAL recast -console levels\mp_rr_canyonlands_mu2.gset 1
start /ABOVENORMAL recast -console levels\mp_rr_canyonlands_mu2_tt.gset 1
start /ABOVENORMAL recast -console levels\mp_rr_canyonlands_staging.gset 1
start /ABOVENORMAL recast -console levels\mp_rr_desertlands_64k_x_64k.gset 1
start /ABOVENORMAL recast -console levels\mp_rr_desertlands_64k_x_64k_nx.gset 1
start /ABOVENORMAL recast -console levels\mp_rr_desertlands_holiday.gset 1
start /ABOVENORMAL recast -console levels\mp_rr_desertlands_mu1.gset 1
start /ABOVENORMAL recast -console levels\mp_rr_desertlands_mu1_tt.gset 1
start /ABOVENORMAL recast -console levels\mp_rr_desertlands_mu2.gset 1
start /ABOVENORMAL recast -console levels\mp_rr_olympus.gset 1
start /ABOVENORMAL recast -console levels\mp_rr_olympus_tt.gset 1
start /ABOVENORMAL recast -console levels\mp_rr_party_crasher.gset 1

REM Copy NavMesh for identical levels.
copy /y /v %~dp0..\maps\navmesh\mp_rr_desertlands_64k_x_64k_small.nm       %~dp0..\maps\navmesh\mp_rr_desertlands_64k_x_64k_nx_small.nm
copy /y /v %~dp0..\maps\navmesh\mp_rr_desertlands_64k_x_64k_med_short.nm   %~dp0..\maps\navmesh\mp_rr_desertlands_64k_x_64k_nx_med_short.nm
copy /y /v %~dp0..\maps\navmesh\mp_rr_desertlands_64k_x_64k_medium.nm      %~dp0..\maps\navmesh\mp_rr_desertlands_64k_x_64k_nx_medium.nm
copy /y /v %~dp0..\maps\navmesh\mp_rr_desertlands_64k_x_64k_large.nm       %~dp0..\maps\navmesh\mp_rr_desertlands_64k_x_64k_nx_large.nm
copy /y /v %~dp0..\maps\navmesh\mp_rr_desertlands_64k_x_64k_extra_large.nm %~dp0..\maps\navmesh\mp_rr_desertlands_64k_x_64k_nx_extra_large.nm

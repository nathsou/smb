pub(all) struct ProgramAnalysis {
  vectors: VectorRoots
  cfg: ControlFlowGraph
  reachability: Reachability
  entries: Array[Int]
  views: Array[RoutineView]
  flags: FlagAnalysis
  liveness: FlagLiveness
  dispatches: Array[DispatchSite]
  dispatcher: DispatcherPlan
} derive(Debug)

// Run discovery in two stages. The first stage finds dispatcher helpers from
// vector-reachable code; the second treats their resolved table entries as
// roots, so computed routine entries participate in all later analyses.
pub fn analyze_program(image: @layout.Image) -> ProgramAnalysis raise DecodeError {
  let vectors = recover_vectors(image)
  let initial_cfg = recover_cfg(image, vectors.entries())
  let initial_dispatches = resolve_inline_dispatches(image, initial_cfg)
  let roots = vectors.entries()
  for dispatch in initial_dispatches {
    for target in dispatch.targets {
      if !roots.contains(target) {
        roots.push(target)
      }
    }
  }
  roots.sort()
  let cfg = recover_cfg(image, roots)
  let dispatches = resolve_inline_dispatches(image, cfg)
  let entries = direct_entries(cfg, roots)
  let views = recover_routine_views(cfg, entries)
  {
    vectors,
    reachability: classify_reachability(image, cfg),
    flags: analyze_flags(image, cfg),
    liveness: analyze_flag_liveness(cfg),
    dispatcher: plan_dispatcher(cfg, views),
    cfg,
    entries,
    views,
    dispatches,
  }
}

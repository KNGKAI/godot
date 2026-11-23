extends RigidBody3D

var ticks := 0
var backend := ""
var min_step := INF
var max_step := 0.0
var integrated_x := 0.0

func _integrate_forces(state: PhysicsDirectBodyState3D) -> void:
	ticks += 1
	backend = state.get_class()
	min_step = minf(min_step, state.step)
	max_step = maxf(max_step, state.step)
	integrated_x = state.transform.origin.x

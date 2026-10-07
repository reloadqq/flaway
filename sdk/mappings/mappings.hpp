#ifndef MAPPINGS_HPP
#define MAPPINGS_HPP
#include <memory>

#include <string>
namespace sdk
{
	namespace mappings
	{
		inline constexpr const char* version = "1.21.10";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html
		inline constexpr const char* minecraftclass_sig = "net/minecraft/class_310";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html#instance
		inline constexpr const char* minecraftclient_name = "field_1700";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html#instance
		inline constexpr const char* minecraftclient_sig = "Lnet/minecraft/class_310;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html#player
		inline constexpr const char* player_name = "field_1724";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html#player
		inline constexpr const char* player_sig = "Lnet/minecraft/class_746;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html#world
		inline constexpr const char* world_name = "field_1687";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html#world
		inline constexpr const char* world_sig = "Lnet/minecraft/class_638;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html#crosshairTarget
		inline constexpr const char* crosshair_target_name = "field_1765";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html#crosshairTarget
		inline constexpr const char* crosshair_target_sig = "Lnet/minecraft/class_239;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html#interactionManager
		inline constexpr const char* interaction_manager_name = "field_1761";

		inline constexpr const char* interaction_manager_class_sig = "net/minecraft/class_636";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html#interactionManager
		inline constexpr const char* interaction_manager_sig = "Lnet/minecraft/class_636;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html#getNetworkHandler()
		inline constexpr const char* network_handler_name = "method_1562";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html#getNetworkHandler()
		inline constexpr const char* network_handler_sig = "()Lnet/minecraft/class_634;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html#connection
		inline constexpr const char* connection_name = "field_1746";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html#connection
		inline constexpr const char* connection_sig = "Lnet/minecraft/class_2535;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html#doAttack()
		inline constexpr const char* do_attack_name = "method_1536";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html#doAttack()
		inline constexpr const char* do_attack_sig = "()Z";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html#attackCooldown
		inline constexpr const char* attack_cooldown_name = "field_1771";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html#attackCooldown
		inline constexpr const char* attack_cooldown_sig = "I";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/render/GameRenderer.html
		inline constexpr const char* gamerenderer_class_sig = "net/minecraft/class_757";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html#gameRenderer
		inline constexpr const char* gamerenderer_name = "field_1773";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html#gameRenderer
		inline constexpr const char* gamerenderer_sig = "Lnet/minecraft/class_757;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/render/GameRenderer.html#updateCrosshairTarget(float)
		inline constexpr const char* update_crosshair_target_name = "method_3190";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/render/GameRenderer.html#updateCrosshairTarget(float)
		inline constexpr const char* update_crosshair_target_sig = "(F)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/render/GameRenderer.html#getFov(net.minecraft.client.render.Camera,float,boolean)
		inline constexpr const char* get_fov_name = "method_3196";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/render/GameRenderer.html#getFov(net.minecraft.client.render.Camera,float,boolean)
		inline constexpr const char* get_fov_sig = "(Lnet/minecraft/class_4184;FZ)F";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/render/GameRenderer.html#getCamera()
		inline constexpr const char* get_camera_name = "method_19418";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/render/GameRenderer.html#getCamera()
		inline constexpr const char* get_camera_sig = "()Lnet/minecraft/class_4184;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/render/Camera.html
		inline constexpr const char* camera_class_sig = "net/minecraft/class_4184";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/render/Camera.html#getPos()
		inline constexpr const char* camera_get_pos_name = "method_19326";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/render/Camera.html#getPos()
		inline constexpr const char* camera_get_pos_sig = "()Lnet/minecraft/class_243;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/render/Camera.html#getYaw()
		inline constexpr const char* camera_get_yaw_name = "method_19330";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/render/Camera.html#getYaw()
		inline constexpr const char* camera_get_yaw_sig = "()F";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/render/Camera.html#getPitch()
		inline constexpr const char* camera_get_pitch_name = "method_19329";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/render/Camera.html#getPitch()
		inline constexpr const char* camera_get_pitch_sig = "()F";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Vec3d.html
		inline constexpr const char* vec3d_class_sig = "net/minecraft/class_243";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Vec3d.html#x
		inline constexpr const char* vec3d_x_name = "field_1352";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Vec3d.html#x
		inline constexpr const char* vec3d_x_sig = "D";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Vec3d.html#y
		inline constexpr const char* vec3d_y_name = "field_1351";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Vec3d.html#y
		inline constexpr const char* vec3d_y_sig = "D";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Vec3d.html#z
		inline constexpr const char* vec3d_z_name = "field_1350";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Vec3d.html#z
		inline constexpr const char* vec3d_z_sig = "D";
		// PlayerEntity.getGameProfile() -> com.mojang.authlib.GameProfile (real account NICK) (verified against 1.21.10 PlayerEntity mapping)
		inline constexpr const char* player_get_game_profile_name = "method_7334";
		inline constexpr const char* player_get_game_profile_sig = "()Lcom/mojang/authlib/GameProfile;";
		// GameProfile.getName() is a Mojang authlib method (not remapped by Yarn)
		inline constexpr const char* game_profile_class_sig = "com/mojang/authlib/GameProfile";
		inline constexpr const char* game_profile_get_name_name = "getName";
		inline constexpr const char* game_profile_get_name_sig = "()Ljava/lang/String;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/network/ClientPlayerEntity.html
		inline constexpr const char* clientplayerentity_class_sig = "net/minecraft/class_746";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/network/ClientPlayerEntity.html#sendMovementPackets()
		inline constexpr const char* send_movement_packets_name = "method_3136";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/network/ClientPlayerEntity.html#sendMovementPackets()
		inline constexpr const char* send_movement_packets_sig = "()V";
		// ClientPlayerEntity.sendSprintingPacket() = method_46742 (private; verified against 1.21.10 bytecode:
		// sends a PlayerMoveC2SPacket sprint step). JNI method calls bypass Java access checks.
		inline constexpr const char* send_sprinting_packet_name = "method_46742";
		inline constexpr const char* send_sprinting_packet_sig = "()V";
		// PlayerEntity.closeHandledScreen() = method_7346 (verified against Yarn 1.21.10 PlayerEntity javadoc;
		// sends a CloseHandledScreenC2SPacket to the server then closes the client screen)
		inline constexpr const char* close_handled_screen_name = "method_7346";
		inline constexpr const char* close_handled_screen_sig = "()V";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/network/ClientPlayerEntity.html#renderYaw
		inline constexpr const char* render_yaw_name = "field_3932";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/network/ClientPlayerEntity.html#renderYaw
		inline constexpr const char* render_yaw_sig = "F";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/network/ClientPlayerEntity.html#renderPitch
		inline constexpr const char* render_pitch_name = "field_3916";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/network/ClientPlayerEntity.html#renderPitch
		inline constexpr const char* render_pitch_sig = "F";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/network/packet/c2s/play/PlayerMoveC2SPacket.html
		inline constexpr const char* playermovec2spacket_class_sig = "net/minecraft/class_2828";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/network/packet/c2s/play/PlayerMoveC2SPacket.html#yaw
		inline constexpr const char* playermovec2spacket_yaw_name = "field_12887";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/network/packet/c2s/play/PlayerMoveC2SPacket.html#yaw
		inline constexpr const char* playermovec2spacket_yaw_sig = "F";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/network/packet/c2s/play/PlayerMoveC2SPacket.html#pitch
		inline constexpr const char* playermovec2spacket_pitch_name = "field_12885";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/network/packet/c2s/play/PlayerMoveC2SPacket.html#pitch
		inline constexpr const char* playermovec2spacket_pitch_sig = "F";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerEntity.html#abilities
		inline constexpr const char* abilities_name = "field_7503";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerEntity.html#abilities
		inline constexpr const char* abilities_sig = "Lnet/minecraft/class_1656;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerEntity.html#getEntityInteractionRange()
		inline constexpr const char* get_entity_interaction_range_name = "method_55755";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerEntity.html#getEntityInteractionRange()
		inline constexpr const char* get_entity_interaction_range_sig = "()D";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerEntity.html#getAttackCooldownProgress(float)
		inline constexpr const char* get_attack_cooldown_progress_name = "method_7261";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerEntity.html#getAttackCooldownProgress(float)
		inline constexpr const char* get_attack_cooldown_progress_sig = "(F)F";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerEntity.html
		inline constexpr const char* player_entity_class_sig = "net/minecraft/class_1657";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerAbilities.html#flying
		inline constexpr const char* fly_name = "field_7479";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerAbilities.html#flying
		inline constexpr const char* fly_sig = "Z";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#getX()
		inline constexpr const char* entity_get_x_name = "method_23317";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#getX()
		inline constexpr const char* entity_get_x_sig = "()D";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#getY()
		inline constexpr const char* entity_get_y_name = "method_23318";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#getY()
		inline constexpr const char* entity_get_y_sig = "()D";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#getZ()
		inline constexpr const char* entity_get_z_name = "method_23321";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#getZ()
		inline constexpr const char* entity_get_z_sig = "()D";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#getYaw()
		inline constexpr const char* entity_get_yaw_name = "method_36454";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#getYaw()
		inline constexpr const char* entity_get_yaw_sig = "()F";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#getPitch()
		inline constexpr const char* entity_get_pitch_name = "method_36455";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#getPitch()
		inline constexpr const char* entity_get_pitch_sig = "()F";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#setYaw(float)
		inline constexpr const char* entity_set_yaw_name = "method_36456";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#setYaw(float)
		inline constexpr const char* entity_set_yaw_sig = "(F)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#setPitch(float)
		inline constexpr const char* entity_set_pitch_name = "method_36457";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#setPitch(float)
		inline constexpr const char* entity_set_pitch_sig = "(F)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#getBoundingBox()
		inline constexpr const char* get_bounding_box_name = "field_6005";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#getBoundingBox()
		inline constexpr const char* get_bounding_box_sig = "Lnet/minecraft/class_238;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#setBoundingBox(net.minecraft.util.math.Box)
		inline constexpr const char* set_bounding_box_name = "method_5857";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#setBoundingBox(net.minecraft.util.math.Box)
		inline constexpr const char* set_bounding_box_sig = "(Lnet/minecraft/class_238;)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html
		inline constexpr const char* entity_class_sig = "net/minecraft/class_1297";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#setFlag(int,boolean)
		inline constexpr const char* entity_set_flag_name = "method_5729";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#setFlag(int,boolean)
		inline constexpr const char* entity_set_flag_sig = "(IZ)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Box.html#minX
		inline constexpr const char* box_class_sig = "net/minecraft/class_238";
		inline constexpr const char* box_min_x_name = "field_1323";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Box.html#minX
		inline constexpr const char* box_min_x_sig = "D";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Box.html#maxX
		inline constexpr const char* box_max_x_name = "field_1320";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Box.html#maxX
		inline constexpr const char* box_max_x_sig = "D";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Box.html#minY
		inline constexpr const char* box_min_y_name = "field_1322";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Box.html#minY
		inline constexpr const char* box_min_y_sig = "D";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Box.html#maxY
		inline constexpr const char* box_max_y_name = "field_1325";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Box.html#maxY
		inline constexpr const char* box_max_y_sig = "D";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Box.html#minZ
		inline constexpr const char* box_min_z_name = "field_1321";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Box.html#minZ
		inline constexpr const char* box_min_z_sig = "D";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Box.html#maxZ
		inline constexpr const char* box_max_z_name = "field_1324";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Box.html#maxZ
		inline constexpr const char* box_max_z_sig = "D";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/world/ClientWorld.html#players
		inline constexpr const char* players_field_name = "field_18226";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/world/ClientWorld.html#players
		inline constexpr const char* players_field_sig = "Ljava/util/List;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/LivingEntity.html
		inline constexpr const char* living_entity_class_sig = "net/minecraft/class_1309";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/LivingEntity.html#isBlocking()
		inline constexpr const char* living_entity_is_blocking_name = "method_6039";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/LivingEntity.html#isBlocking()
		inline constexpr const char* living_entity_is_blocking_sig = "()Z";
		// LivingEntity.hasStatusEffect(RegistryEntry<StatusEffect>) — 1.21.10
		inline constexpr const char* living_entity_has_status_effect_name = "method_6059";
		inline constexpr const char* living_entity_has_status_effect_sig = "(Lnet/minecraft/class_6880;)Z";
		// StatusEffects (registry class).POISON — intermediary Class/field names
		inline constexpr const char* status_effects_class_sig = "net/minecraft/class_1294";
		inline constexpr const char* status_effects_poison_field = "field_5899";
		// LivingEntity.getActiveItem() = method_6030 (verified: returns usingItem field_6277 against 1.21.10 bytecode)
		inline constexpr const char* living_get_active_item_name = "method_6030";
		inline constexpr const char* living_get_active_item_sig = "()Lnet/minecraft/class_1799;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/LivingEntity.html#setSprinting(boolean)
		inline constexpr const char* set_sprinting_name = "method_5728";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/LivingEntity.html#setSprinting(boolean)
		inline constexpr const char* set_sprinting_sig = "(Z)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#isSprinting()
		inline constexpr const char* is_sprinting_name = "method_5624";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#isSprinting()
		inline constexpr const char* is_sprinting_sig = "()Z";
		// Entity.setSwimming(boolean) = method_5796 (verified: sets flag 4 via method_5729 against 1.21.10 Entity bytecode)
		inline constexpr const char* set_swimming_name = "method_5796";
		inline constexpr const char* set_swimming_sig = "(Z)V";
		// Entity.isTouchingWater() = method_5799 (verified against Yarn 1.21.10 Entity javadoc; returns cached field_5957.
		// NOTE: method_5771 is isInLava() — do NOT use it for water detection)
		inline constexpr const char* is_touching_water_name = "method_5799";
		inline constexpr const char* is_touching_water_sig = "()Z";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#isOnGround()
		inline constexpr const char* is_on_ground_name = "method_24828";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#isOnGround()
		inline constexpr const char* is_on_ground_sig = "()Z";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#getId()
		// EntityLike.getId() = method_5628 (verified against 1.21.10 Entity javadoc)
		inline constexpr const char* entity_get_id_name = "method_5628";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#getId()
		inline constexpr const char* entity_get_id_sig = "()I";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#setVelocity(net.minecraft.util.math.Vec3d)
		inline constexpr const char* entity_set_velocity_name = "method_18800";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#setVelocity(net.minecraft.util.math.Vec3d)
		inline constexpr const char* entity_set_velocity_sig = "(DDD)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#velocity
		inline constexpr const char* entity_velocity_name = "field_18276";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#velocity
		inline constexpr const char* entity_velocity_sig = "Lnet/minecraft/class_243;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#handleFallDamage(double,float,net.minecraft.entity.damage.DamageSource)
		inline constexpr const char* entity_handle_fall_damage_name = "method_5747";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#handleFallDamage(double,float,net.minecraft.entity.damage.DamageSource)
		inline constexpr const char* entity_handle_fall_damage_sig = "(DFLnet/minecraft/class_1282;)Z";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#fallDistance
		inline constexpr const char* entity_fall_distance_name = "field_6017";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#fallDistance
		inline constexpr const char* entity_fall_distance_sig = "D";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerEntity.html#attack(net.minecraft.entity.Entity)
		inline constexpr const char* player_attack_name = "method_7324";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerEntity.html#attack(net.minecraft.entity.Entity)
		inline constexpr const char* player_attack_sig = "(Lnet/minecraft/class_1297;)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/LivingEntity.html#attackLivingEntity(net.minecraft.entity.LivingEntity)
		inline constexpr const char* player_attack_living_entity_name = "method_5997";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/LivingEntity.html#attackLivingEntity(net.minecraft.entity.LivingEntity)
		inline constexpr const char* player_attack_living_entity_sig = "(Lnet/minecraft/class_1309;)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerEntity.html#inventory
		inline constexpr const char* player_inventory_name = "field_7514";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerEntity.html#inventory
		inline constexpr const char* player_inventory_sig = "Lnet/minecraft/class_1661;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerInventory.html
		inline constexpr const char* player_inventory_class_sig = "net/minecraft/class_1661";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerInventory.html#selectedSlot
		inline constexpr const char* inventory_selected_slot_name = "field_7545";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerInventory.html#selectedSlot
		inline constexpr const char* inventory_selected_slot_sig = "I";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerInventory.html#getStack(int)
		inline constexpr const char* inventory_get_stack_name = "method_5438";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerInventory.html#getStack(int)
		inline constexpr const char* inventory_get_stack_sig = "(I)Lnet/minecraft/class_1799;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerInventory.html#setStack(int,net.minecraft.item.ItemStack)
		inline constexpr const char* inventory_set_stack_name = "method_5447";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerInventory.html#setStack(int,net.minecraft.item.ItemStack)
		inline constexpr const char* inventory_set_stack_sig = "(ILnet/minecraft/class_1799;)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/item/ItemStack.html#getItem()
		inline constexpr const char* itemstack_get_item_name = "method_7909";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/item/ItemStack.html#getItem()
		inline constexpr const char* itemstack_get_item_sig = "()Lnet/minecraft/class_1792;";
		// ItemStack.getName() -> Text (display name, honors custom names); = method_7964 (verified)
		inline constexpr const char* itemstack_get_name_name = "method_7964";
		inline constexpr const char* itemstack_get_name_sig = "()Lnet/minecraft/class_2561;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/item/ItemStack.html#isEmpty()
		inline constexpr const char* itemstack_is_empty_name = "method_7960";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/item/ItemStack.html#isEmpty()
		inline constexpr const char* itemstack_is_empty_sig = "()Z";
		// ItemStack.getRarity() -> Rarity (icon color for Item ESP) (verified against 1.21.10 ItemStack mapping)
		inline constexpr const char* itemstack_get_rarity_name = "method_7932";
		inline constexpr const char* itemstack_get_rarity_sig = "()Lnet/minecraft/class_1814;";
		// Rarity.index (I) = ordinal: 0 common, 1 uncommon, 2 rare, 3 epic (verified against 1.21.10 Rarity mapping)
		inline constexpr const char* rarity_class_sig = "net/minecraft/class_1814";
		inline constexpr const char* rarity_index_name = "field_50004";
		inline constexpr const char* rarity_index_sig = "I";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/item/Item.html
		inline constexpr const char* item_class_sig = "net/minecraft/class_1792";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/item/Item.html#getTranslationKey()
		inline constexpr const char* item_get_translation_key_name = "method_7876";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/item/Item.html#getTranslationKey()
		inline constexpr const char* item_get_translation_key_sig = "()Ljava/lang/String;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/network/ClientPlayerInteractionManager.html#interactItem(net.minecraft.entity.player.PlayerEntity,net.minecraft.util.Hand)
		inline constexpr const char* interact_item_name = "method_2919";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/network/ClientPlayerInteractionManager.html#interactItem(net.minecraft.entity.player.PlayerEntity,net.minecraft.util.Hand)
		inline constexpr const char* interact_item_sig = "(Lnet/minecraft/class_1657;Lnet/minecraft/class_1268;)Lnet/minecraft/class_1269;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/Hand.html
		inline constexpr const char* hand_class_sig = "net/minecraft/class_1268";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/Hand.html#MAIN_HAND
		inline constexpr const char* hand_main_hand_name = "field_5808";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/Hand.html#MAIN_HAND
		inline constexpr const char* hand_main_hand_sig = "Lnet/minecraft/class_1268;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/hit/EntityHitResult.html
		inline constexpr const char* entity_hit_result_class_sig = "net/minecraft/class_3966";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/hit/EntityHitResult.html#getEntity()
		inline constexpr const char* entity_hit_result_get_entity_name = "method_17782";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/hit/EntityHitResult.html#getEntity()
		inline constexpr const char* entity_hit_result_get_entity_sig = "()Lnet/minecraft/class_1297;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/LivingEntity.html#getHealth()
		inline constexpr const char* living_entity_get_health_name = "method_6032";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/LivingEntity.html#getHealth()
		inline constexpr const char* living_entity_get_health_sig = "()F";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/LivingEntity.html#getMaxHealth()
		inline constexpr const char* living_entity_get_max_health_name = "method_6063";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/LivingEntity.html#getMaxHealth()
		inline constexpr const char* living_entity_get_max_health_sig = "()F";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/LivingEntity.html#hurtTime
		inline constexpr const char* living_entity_hurt_time_name = "field_6235";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/LivingEntity.html#hurtTime
		inline constexpr const char* living_entity_hurt_time_sig = "I";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerInventory.html#offHand
		inline constexpr const char* inventory_offhand_name = "field_30639";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/player/PlayerInventory.html#offHand
		inline constexpr const char* inventory_offhand_sig = "I";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/world/ClientWorld.html
		inline constexpr const char* client_world_class_sig = "net/minecraft/class_638";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/world/ClientWorld.html#blockEntities
		inline constexpr const char* client_world_block_entities_name = "field_60919";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/world/ClientWorld.html#blockEntities
		inline constexpr const char* client_world_block_entities_sig = "Ljava/util/Set;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/block/entity/BlockEntity.html
		inline constexpr const char* block_entity_class_sig = "net/minecraft/class_2586";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/block/entity/BlockEntity.html#getPos()
		inline constexpr const char* block_entity_get_pos_name = "method_11016";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/block/entity/BlockEntity.html#getPos()
		inline constexpr const char* block_entity_get_pos_sig = "()Lnet/minecraft/class_2338;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/BlockPos.html
		inline constexpr const char* block_pos_class_sig = "net/minecraft/class_2338";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/BlockPos.html#getX()
		inline constexpr const char* block_pos_get_x_name = "method_10263";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/BlockPos.html#getX()
		inline constexpr const char* block_pos_get_x_sig = "()I";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/BlockPos.html#getY()
		inline constexpr const char* block_pos_get_y_name = "method_10264";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/BlockPos.html#getY()
		inline constexpr const char* block_pos_get_y_sig = "()I";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/BlockPos.html#getZ()
		inline constexpr const char* block_pos_get_z_name = "method_10260";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/BlockPos.html#getZ()
		inline constexpr const char* block_pos_get_z_sig = "()I";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/world/World.html
		inline constexpr const char* world_class_sig = "net/minecraft/class_1937";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/world/BlockView.html#getBlockState(net.minecraft.util.math.BlockPos)
		inline constexpr const char* world_get_block_state_name = "method_8320";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/world/BlockView.html#getBlockState(net.minecraft.util.math.BlockPos)
		inline constexpr const char* world_get_block_state_sig = "(Lnet/minecraft/class_2338;)Lnet/minecraft/class_2680;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/block/AbstractBlock.AbstractBlockState.html
		inline constexpr const char* block_state_class_sig = "net/minecraft/class_4970$class_4971";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/block/AbstractBlock.AbstractBlockState.html#getBlock()
		inline constexpr const char* block_state_get_block_name = "method_26204";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/block/AbstractBlock.AbstractBlockState.html#getBlock()
		inline constexpr const char* block_state_get_block_sig = "()Lnet/minecraft/class_2248;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/block/entity/ChestBlockEntity.html
		inline constexpr const char* chest_block_entity_class_sig = "net/minecraft/class_2595";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/block/entity/EnderChestBlockEntity.html
		inline constexpr const char* ender_chest_block_entity_class_sig = "net/minecraft/class_2611";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/block/entity/ShulkerBoxBlockEntity.html
		inline constexpr const char* shulker_box_block_entity_class_sig = "net/minecraft/class_2627";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/block/entity/LockableContainerBlockEntity.html
		inline constexpr const char* container_block_entity_class_sig = "net/minecraft/class_2624";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/hit/BlockHitResult.html
		inline constexpr const char* block_hit_result_class_sig = "net/minecraft/class_3965";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/hit/BlockHitResult.html#getBlockPos()
		inline constexpr const char* block_hit_result_get_block_pos_name = "method_17777";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/hit/BlockHitResult.html#getBlockPos()
		inline constexpr const char* block_hit_result_get_block_pos_sig = "()Lnet/minecraft/class_2338;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/hit/HitResult.html#getType()
		inline constexpr const char* block_hit_result_get_type_name = "method_17783";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/hit/HitResult.html#getType()
		inline constexpr const char* block_hit_result_get_type_sig = "()Lnet/minecraft/class_239$class_240;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/block/ObsidianBlock.html
		inline constexpr const char* obsidian_block_class_sig = "net/minecraft/class_663$class_11926";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/network/ClientPlayNetworkHandler.html
		inline constexpr const char* network_handler_class_sig = "net/minecraft/class_634";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/network/ClientCommonNetworkHandler.html#sendPacket(net.minecraft.network.Packet)
		inline constexpr const char* send_packet_name = "method_52787";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/network/ClientCommonNetworkHandler.html#sendPacket(net.minecraft.network.Packet)
		inline constexpr const char* send_packet_sig = "(Lnet/minecraft/class_2596;)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/network/packet/c2s/play/UpdateSelectedSlotC2SPacket.html
		inline constexpr const char* update_selected_slot_c2s_packet_class_sig = "net/minecraft/class_2868";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/network/packet/c2s/play/PlayerActionC2SPacket.html
		inline constexpr const char* player_action_c2s_packet_class_sig = "net/minecraft/class_2846";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/network/packet/c2s/play/PlayerActionC2SPacket$Action.html
		inline constexpr const char* player_action_c2s_packet_action_class_sig = "net/minecraft/class_2846$class_2847";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/network/packet/c2s/play/PlayerActionC2SPacket$Action.html#SWAP_ITEM_WITH_OFFHAND
		inline constexpr const char* swap_item_with_offhand_action_name = "field_12969";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/network/packet/c2s/play/PlayerActionC2SPacket$Action.html#SWAP_ITEM_WITH_OFFHAND
		inline constexpr const char* swap_item_with_offhand_action_sig = "Lnet/minecraft/class_2846$class_2847;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/network/packet/c2s/play/UpdateSelectedSlotC2SPacket.html
		inline constexpr const char* pick_from_inventory_c2s_packet_class_sig = "net/minecraft/class_2868";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/BlockPos.html#ORIGIN
		inline constexpr const char* block_pos_origin_name = "field_10980";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/BlockPos.html#ORIGIN
		inline constexpr const char* block_pos_origin_sig = "Lnet/minecraft/class_2338;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Direction.html
		inline constexpr const char* direction_class_sig = "net/minecraft/class_2350";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Direction.html#DOWN
		inline constexpr const char* direction_down_name = "field_11033";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/util/math/Direction.html#DOWN
		inline constexpr const char* direction_down_sig = "Lnet/minecraft/class_2350;";
		inline constexpr const char* channel_inbound_handler_adapter_class_sig = "io/netty/channel/ChannelInboundHandlerAdapter";
		inline constexpr const char* channel_read0_name = "channelRead0";
		inline constexpr const char* channel_read0_sig = "(Lio/netty/channel/ChannelHandlerContext;Ljava/lang/Object;)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/network/packet/s2c/play/EntityS2CPacket.html
		inline constexpr const char* entity_s2c_packet_class_sig = "net/minecraft/class_2684";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/network/packet/s2c/play/EntityPositionS2CPacket.html
		inline constexpr const char* entity_position_s2c_packet_class_sig = "net/minecraft/class_2777";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/network/packet/s2c/play/EntityMoveS2CPacket.html
		inline constexpr const char* entity_move_s2c_packet_class_sig = "net/minecraft/class_2777";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/network/packet/s2c/play/EntityTeleportS2CPacket.html
		inline constexpr const char* entity_teleport_s2c_packet_class_sig = "net/minecraft/class_2777";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/entity/Entity.html#getName()
		// Entity.getName() = Nameable.getName() = method_5477 (verified against 1.21.10 Entity javadoc)
		inline constexpr const char* entity_get_name_name = "method_5477";
		inline constexpr const char* entity_get_name_sig = "()Lnet/minecraft/class_2561;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/text/Text.html#asTruncatedString(int)
		// Text.getString() has no Yarn name; use asTruncatedString(int) = method_10858, pass Integer.MAX_VALUE
		inline constexpr const char* text_class_sig = "net/minecraft/class_2561";
		inline constexpr const char* text_get_string_name = "method_10858";
		inline constexpr const char* text_get_string_sig = "(I)Ljava/lang/String;";
		// Chest Stealer / Screen mappings
		inline constexpr const char* screen_class_sig = "net/minecraft/class_437";
		// MinecraftClient.currentScreen = field_1755 (verified against Yarn 1.21.10 javadoc:
		// Lnet/minecraft/class_310;field_1755:Lnet/minecraft/class_437;). field_1753 does NOT exist.
		inline constexpr const char* minecraft_screen_name = "field_1755";
		inline constexpr const char* minecraft_screen_sig = "Lnet/minecraft/class_437;";
		inline constexpr const char* handled_screen_class_sig = "net/minecraft/class_465";
		// HandledScreen.handler = field_2797 (verified against Yarn 1.21.10 javadoc:
		// Lnet/minecraft/class_465;field_2797:Lnet/minecraft/class_1703;). field_1728 is NOT on HandledScreen.
		inline constexpr const char* screen_handler_name = "field_2797";
		inline constexpr const char* screen_handler_sig = "Lnet/minecraft/class_1703;";
		inline constexpr const char* screen_handler_class_sig = "net/minecraft/class_1703";
		// ScreenHandler.getStacks() = method_7602 (returns DefaultedList of all stacks;
		// ScreenHandler has NO size() in 1.21.10 — slot count is derived from this list)
		inline constexpr const char* screen_handler_get_stacks_name = "method_7602";
		inline constexpr const char* screen_handler_get_stacks_sig = "()Lnet/minecraft/class_2371;";
		// ScreenHandler.getSlot(int) = method_7611 (verified against Yarn 1.21.10 javadoc)
		inline constexpr const char* screen_handler_get_slot_name = "method_7611";
		inline constexpr const char* screen_handler_get_slot_sig = "(I)Lnet/minecraft/class_1735;";
		inline constexpr const char* slot_class_sig = "net/minecraft/class_1735";
		// Slot.hasStack() = method_7681 (verified against Yarn 1.21.10 javadoc)
		inline constexpr const char* slot_has_stack_name = "method_7681";
		inline constexpr const char* slot_has_stack_sig = "()Z";
		// Slot.getStack() = method_7677 (verified against Yarn 1.21.10 javadoc)
		inline constexpr const char* slot_get_stack_name = "method_7677";
		inline constexpr const char* slot_get_stack_sig = "()Lnet/minecraft/class_1799;";
		// ScreenHandler.onSlotClick(int,int,SlotActionType,PlayerEntity) = method_7593
		// (verified against Yarn 1.21.10 javadoc; NOT method_7603 which is removeListener())
		inline constexpr const char* screen_handler_click_slot_name = "method_7593";
		inline constexpr const char* screen_handler_click_slot_sig = "(IILnet/minecraft/class_1713;Lnet/minecraft/class_1657;)V";
		inline constexpr const char* slot_action_type_class_sig = "net/minecraft/class_1713";
		inline constexpr const char* slot_action_type_quick_move_name = "field_7794";
		// ItemEntity.getStack() = method_6983 (verified against 1.21.10 ItemEntity mapping)
		inline constexpr const char* item_entity_get_stack_name = "method_6983";
		inline constexpr const char* item_entity_get_stack_sig = "()Lnet/minecraft/class_1799;";
		// Item ESP / Entity iteration
		inline constexpr const char* item_entity_class_sig = "net/minecraft/class_1542";
		// ClientWorld.getEntities() = method_18112 -> Iterable (verified against 1.21.10 ClientWorld mapping)
		inline constexpr const char* world_get_entities_name = "method_18112";
		inline constexpr const char* world_get_entities_sig = "()Ljava/lang/Iterable;";
		// World.getEntitiesByClass does not exist in 1.21.10; filter the Iterable with IsInstanceOf instead.
		inline constexpr const char* world_get_entities_by_class_name = "method_18112";
		inline constexpr const char* world_get_entities_by_class_sig = "()Ljava/lang/Iterable;";
		// Fullbright / GameOptions
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/MinecraftClient.html#options
		inline constexpr const char* game_options_name = "field_1690";
		inline constexpr const char* game_options_sig = "Lnet/minecraft/class_315;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/option/GameOptions.html
		inline constexpr const char* game_options_class_sig = "net/minecraft/class_315";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/option/GameOptions.html#gamma
		inline constexpr const char* game_options_gamma_name = "field_1840";
		inline constexpr const char* game_options_gamma_sig = "Lnet/minecraft/class_7172;";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/option/SimpleOption.html
		inline constexpr const char* simple_option_class_sig = "net/minecraft/class_7172";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/option/SimpleOption.html#value
		inline constexpr const char* simple_option_value_name = "field_37868";
		inline constexpr const char* simple_option_value_sig = "Ljava/lang/Object;";
		// EntityRenderer.renderLabelIfPresent = method_3571 (verified against 1.21.10
		// client-intermediary bytecode: renderName method_3569 casts its first arg to
		// class_11964 and delegates here, so suppressing method_3571 hides all labels).
		inline constexpr const char* entity_renderer_class_sig = "net/minecraft/class_828";
		inline constexpr const char* render_label_name = "method_3571";
		inline constexpr const char* render_label_sig = "(Lnet/minecraft/class_11964;Lnet/minecraft/class_4587;Lnet/minecraft/class_11659;Lnet/minecraft/class_12075;)V";

		// BaseFinder — block-world access (all verified against yarn-1.21.10+build.1)
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/world/WorldView.html
		inline constexpr const char* world_is_air_name = "method_22347";
		inline constexpr const char* world_is_air_sig = "(Lnet/minecraft/class_2338;)Z";
		inline constexpr const char* world_is_chunk_loaded_ii_name = "method_8393";
		inline constexpr const char* world_is_chunk_loaded_ii_sig = "(II)Z";
		inline constexpr const char* world_is_chunk_loaded_bp_name = "method_22340";
		inline constexpr const char* world_is_chunk_loaded_bp_sig = "(Lnet/minecraft/class_2338;)Z";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/world/HeightLimitView.html
		inline constexpr const char* world_get_bottom_y_name = "method_31607";
		inline constexpr const char* world_get_bottom_y_sig = "()I";
		inline constexpr const char* world_get_top_y_inclusive_name = "method_31600";
		inline constexpr const char* world_get_top_y_inclusive_sig = "()I";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/world/BlockRenderView.html#getLightLevel
		inline constexpr const char* world_get_light_level_name = "method_8314";
		inline constexpr const char* world_get_light_level_sig = "(Lnet/minecraft/class_1944;Lnet/minecraft/class_2338;)I";
		inline constexpr const char* light_type_class_sig = "net/minecraft/class_1944";
		inline constexpr const char* light_type_block_field = "field_9282";
		inline constexpr const char* light_type_block_sig = "Lnet/minecraft/class_1944;";
		// AbstractBlock.AbstractBlockState.isReplaceable() (verified: intermediary method_45474)
		inline constexpr const char* block_state_is_replaceable_name = "method_45474";
		inline constexpr const char* block_state_is_replaceable_sig = "()Z";
		// Block identity: Block.getTranslationKey() = method_9539, fallback cached field translationKey = field_10642
		inline constexpr const char* block_class_sig = "net/minecraft/class_2248";
		inline constexpr const char* block_get_translation_key_name = "method_9539";
		inline constexpr const char* block_get_translation_key_sig = "()Ljava/lang/String;";
		inline constexpr const char* block_translation_key_field = "field_10642";
		inline constexpr const char* block_translation_key_sig = "Ljava/lang/String;";
		// BaseFinder — click mode (place block at found position)
		inline constexpr const char* vec3d_ctor_sig = "(DDD)V";
		inline constexpr const char* block_pos_ctor_sig = "(III)V";
		// Direction.UP = field_11036 (verified against yarn 1.21.10 Direction javadoc)
		inline constexpr const char* direction_up_name = "field_11036";
		inline constexpr const char* direction_up_sig = "Lnet/minecraft/class_2350;";
		// BlockHitResult(Vec3d, Direction, BlockPos, boolean) — ctor not remapped
		inline constexpr const char* block_hit_result_ctor_sig = "(Lnet/minecraft/class_243;Lnet/minecraft/class_2350;Lnet/minecraft/class_2338;Z)V";
		// ClientPlayerInteractionManager.interactBlock(player, hand, hitResult) (verified: method_2896)
		inline constexpr const char* interact_block_name = "method_2896";
		inline constexpr const char* interact_block_sig = "(Lnet/minecraft/class_746;Lnet/minecraft/class_1268;Lnet/minecraft/class_3965;)Lnet/minecraft/class_1269;";

		// Client-side chat line (ChatHud). Adding a message here only paints it
		// in the local chat window — nothing is sent to the server.
		// Chain: MinecraftClient.inGameHud (field_1705) -> InGameHud.getChatHud
		// (method_1743) -> ChatHud.addMessage(Text) (method_1812), built with
		// Text.literal(String) (method_43470). All IDs are identical in
		// yarn-1.21.4, yarn-1.21.8 and yarn-1.21.10.
		inline constexpr const char* ingamehud_class_sig = "net/minecraft/class_329";
		inline constexpr const char* minecraftclient_ingamehud_field = "field_1705";
		inline constexpr const char* minecraftclient_ingamehud_sig = "Lnet/minecraft/class_329;";
		inline constexpr const char* ingamehud_get_chat_hud_name = "method_1743";
		inline constexpr const char* ingamehud_get_chat_hud_sig = "()Lnet/minecraft/class_338;";
		inline constexpr const char* chat_hud_class_sig = "net/minecraft/class_338";
		inline constexpr const char* chat_hud_add_message_name = "method_1812";
		inline constexpr const char* chat_hud_add_message_sig = "(Lnet/minecraft/class_2561;)V";
		inline constexpr const char* text_literal_name = "method_43470";
		inline constexpr const char* text_literal_sig = "(Ljava/lang/String;)Lnet/minecraft/class_5250;";
		inline constexpr const char* mutable_text_class_sig = "net/minecraft/class_5250";

		// ChatScreen (class_408) — sendMessage(String, boolean) intercepts chat input
		inline constexpr const char* chat_screen_class_sig = "net/minecraft/class_408";
		// https://maven.fabricmc.net/docs/yarn-1.21.10+build.1/net/minecraft/client/gui/screen/ChatScreen.html#sendMessage(java.lang.String,boolean)
		inline constexpr const char* chat_screen_send_message_name = "method_44056";
		inline constexpr const char* chat_screen_send_message_sig = "(Ljava/lang/String;Z)V";

		// --- HUD (flaway/gui/hud) — verified against Yarn 1.21.10 mappings ---
		// MinecraftClient.isHudEnabled() — F1 vanilla HUD toggle
		inline constexpr const char* hud_is_hud_enabled_name = "method_1498";
		inline constexpr const char* hud_is_hud_enabled_sig = "()Z";
		// MinecraftClient.getCurrentServerEntry() -> ServerInfo (class_642), null in singleplayer
		inline constexpr const char* current_server_entry_name = "method_1558";
		inline constexpr const char* current_server_entry_sig = "()Lnet/minecraft/class_642;";
		inline constexpr const char* server_info_class_sig = "net/minecraft/class_642";
		inline constexpr const char* server_info_address_name = "field_3761";
		inline constexpr const char* server_info_address_sig = "Ljava/lang/String;";
		// ClientPlayNetworkHandler (class_634).getPlayerListEntry(String) -> PlayerListEntry (class_640)
		inline constexpr const char* player_list_entry_class_sig = "net/minecraft/class_640";
		inline constexpr const char* network_get_entry_by_name_name = "method_2874";
		inline constexpr const char* network_get_entry_by_name_sig = "(Ljava/lang/String;)Lnet/minecraft/class_640;";
		// PlayerListEntry.getLatency() / getProfile()
		inline constexpr const char* entry_get_latency_name = "method_2959";
		inline constexpr const char* entry_get_latency_sig = "()I";
		inline constexpr const char* entry_get_profile_name = "method_2966";
		inline constexpr const char* entry_get_profile_sig = "()Lcom/mojang/authlib/GameProfile;";

	}
};

#endif // MAPPINGS_HPP
#pragma once

#include <sdk/includes.h>

// Shared JNI helpers for the Auction House modules (AutoBuy / AH Helper).
// All helpers are exception-safe: every JNI call is guarded by
// ExceptionCheck/ExceptionClear, and local refs are released before returning.
// Resolved Yarn 1.21.10 (intermediary) mappings are documented in ah_utils.cpp.

namespace flaway
{
	namespace ah_utils
	{
		uint64_t now_ms();
		bool has_passed(uint64_t start, uint64_t delay_ms);

		// ClientPlayNetworkHandler.sendChatCommand(String) = method_45730
		void send_command(JNIEnv* env, const char* command);

		// MinecraftClient.currentScreen (field_1755) -> jobject (or null).
		// Caller owns the returned local ref.
		jobject get_current_screen(JNIEnv* env);

		// Screen.getTitle().getString() = method_25440 / method_10858.
		std::string get_screen_title(JNIEnv* env, jobject screen);

		// HandledScreen.handler (field_2797) -> ScreenHandler (or null).
		// Caller owns the returned local ref.
		jobject get_screen_handler(JNIEnv* env, jobject screen);

		// Total slot count from ScreenHandler.getStacks().size() (method_7602).
		int screen_handler_slot_count(JNIEnv* env, jobject handler);

		// ScreenHandler.getSlot(i).getStack() -> ItemStack (or null).
		// Caller owns the returned local ref.
		jobject get_handler_slot_stack(JNIEnv* env, jobject handler, int slot);

		// ItemStack.getTooltip(Item.TooltipContext, PlayerEntity, TooltipType)
		// = method_7950; joins every line via Text.getString() (method_10858).
		// Empty if the stack/player is invalid.
		std::string stack_tooltip_text(JNIEnv* env, jobject stack, jobject player);

		// ItemStack.getName().getString() (method_7964 / method_10858).
		std::string stack_display_name(JNIEnv* env, jobject stack);

		// ItemStack.getCount() = method_7947 (0 on failure).
		int stack_count(JNIEnv* env, jobject stack);

		// Parses the first number found in `text` that represents a coin price
		// (a digit run followed by the gold coin symbol '¤', or a bare digit
		// run if no '¤' line exists). Returns -1 when nothing matches.
		long parse_price(const std::string& text);

		// Convenience: tooltip -> parse_price. Returns -1 when unavailable.
		long parse_stack_price(JNIEnv* env, jobject stack, jobject player);

		// Reads the player's coin balance from the sidebar scoreboard
		// (World.getScoreboard() = method_8428, ScoreboardDisplaySlot.SIDEBAR).
		// Returns -1 when the scoreboard is unavailable, else >= 0.
		int get_balance(JNIEnv* env, jobject player, jobject world);

		// PlayerEntity.closeHandledScreen() = method_7346.
		void close_screen(JNIEnv* env, jobject player);

		// Resolves a SlotActionType enum constant by ordinal (0 PICKUP,
		// 1 QUICK_MOVE, 3 CLONE). Returns a local ref the caller must release.
		jobject get_slot_action_type(JNIEnv* env, int ordinal);

		// ScreenHandler.onSlotClick(slot, button, SlotActionType, player)
		// = method_7593. `action_ordinal` is the SlotActionType ordinal.
		void click_slot(JNIEnv* env, jobject handler, int slot, int button,
			int action_ordinal, jobject player);
	}
}

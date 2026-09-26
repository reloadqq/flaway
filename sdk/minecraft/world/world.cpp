#include <flaway/flaway.h>
#include "world.h"
#include <sdk/mappings/mappings.hpp>
#include <sdk/classloader.h>

sdk::world_client::world_client(jobject world)
{
	auto env = flaway::instance->get_env();
	if (env && world)
		this->world = env->NewGlobalRef(world);
	else
		this->world = nullptr;
}

sdk::world_client::~world_client()
{
	if (world)
	{
		auto env = flaway::instance->get_env();
		if (env) env->DeleteGlobalRef(world);
	}
}

std::vector<jobject> sdk::world_client::get_players()
{
	std::vector<jobject> players;
	auto env = flaway::instance->get_env();
	if (!env || !world) return players;

	jclass world_class = env->GetObjectClass(world);
	if (!world_class) return players;

	jfieldID fid = env->GetFieldID(world_class, sdk::mappings::players_field_name, sdk::mappings::players_field_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	if (!fid)
	{
		env->DeleteLocalRef(world_class);
		return players;
	}

	jobject players_list = env->GetObjectField(world, fid);
	if (!players_list)
	{
		env->DeleteLocalRef(world_class);
		return players;
	}

	jclass list_class = env->GetObjectClass(players_list);
	if (!list_class)
	{
		env->DeleteLocalRef(world_class);
		env->DeleteLocalRef(players_list);
		return players;
	}

	jmethodID size_method = env->GetMethodID(list_class, "size", "()I");
	if (env->ExceptionCheck()) env->ExceptionClear();
	jmethodID get_method = env->GetMethodID(list_class, "get", "(I)Ljava/lang/Object;");
	if (env->ExceptionCheck()) env->ExceptionClear();

	if (size_method && get_method)
	{
		jint list_size = env->CallIntMethod(players_list, size_method);
		if (env->ExceptionCheck()) { env->ExceptionClear(); list_size = 0; }
		for (jint i = 0; i < list_size; i++)
		{
			jobject player = env->CallObjectMethod(players_list, get_method, i);
			if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
			if (player) players.push_back(player);
		}
	}

	env->DeleteLocalRef(list_class);
	env->DeleteLocalRef(players_list);
	env->DeleteLocalRef(world_class);

	return players;
}

std::vector<jobject> sdk::world_client::get_entities()
{
	std::vector<jobject> entities;
	auto env = flaway::instance->get_env();
	if (!env || !world) return entities;

	jclass world_class = env->GetObjectClass(world);
	if (!world_class) return entities;

	// ClientWorld.getEntities() = method_18112 -> Iterable
	jmethodID get_entities_mid = env->GetMethodID(world_class,
		sdk::mappings::world_get_entities_name,
		sdk::mappings::world_get_entities_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();

	if (get_entities_mid)
	{
		jobject iterable = env->CallObjectMethod(world, get_entities_mid);
		if (env->ExceptionCheck()) { env->ExceptionClear(); }
		else if (iterable)
		{
			jclass iterable_class = env->GetObjectClass(iterable);
			if (iterable_class)
			{
				jmethodID iterator_mid = env->GetMethodID(iterable_class, "iterator", "()Ljava/util/Iterator;");
				if (env->ExceptionCheck()) env->ExceptionClear();
				if (iterator_mid)
				{
					jobject it = env->CallObjectMethod(iterable, iterator_mid);
					if (env->ExceptionCheck()) { env->ExceptionClear(); }
					else if (it)
					{
						jclass iterator_class = env->GetObjectClass(it);
						jmethodID has_next_mid = iterator_class ? env->GetMethodID(iterator_class, "hasNext", "()Z") : nullptr;
						if (env->ExceptionCheck()) env->ExceptionClear();
						jmethodID next_mid = iterator_class ? env->GetMethodID(iterator_class, "next", "()Ljava/lang/Object;") : nullptr;
						if (env->ExceptionCheck()) env->ExceptionClear();
						if (has_next_mid && next_mid)
						{
						while (env->CallBooleanMethod(it, has_next_mid))
						{
							if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
							jobject e = env->CallObjectMethod(it, next_mid);
							if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
							if (e) entities.push_back(e);
						}
						if (env->ExceptionCheck()) env->ExceptionClear();
						}
						if (iterator_class) env->DeleteLocalRef(iterator_class);
						env->DeleteLocalRef(it);
					}
				}
				env->DeleteLocalRef(iterable_class);
			}
			env->DeleteLocalRef(iterable);
		}
	}

	env->DeleteLocalRef(world_class);
	return entities;
}

std::vector<jobject> sdk::world_client::get_entities_by_class(jclass entity_class)
{
	std::vector<jobject> entities;
	auto env = flaway::instance->get_env();
	if (!env || !world || !entity_class) return entities;

	std::vector<jobject> all = get_entities();
	for (jobject e : all)
	{
		if (!e) continue;
		if (env->IsInstanceOf(e, entity_class))
			entities.push_back(e);
		else
			env->DeleteLocalRef(e);
	}
	return entities;
}

#include "jsonFirmware.h"

#include <algorithm>

namespace elektronData::json
{
	namespace
	{
		bool isIn(const std::vector<std::string>& _list, const std::string& _k)
		{
			return std::find(_list.begin(), _list.end(), _k) != _list.end();
		}

		std::pair<std::string, std::string> split(const std::string& _path)
		{
			const auto dot = _path.find('.');
			return dot == std::string::npos ? std::make_pair(_path, std::string()) : std::make_pair(_path.substr(0, dot), _path.substr(dot + 1));
		}

		// _obj without member _key (a copy).
		Value without(const Value& _obj, const std::string& _key)
		{
			Value out = Value::object();
			for(const auto& [k, v] : _obj.asObject())
				if(k != _key)
					out.set(k, v);
			return out;
		}
	}

	Value groupFirmware(const Value& _v1, const FirmwareLayout& _layout, const int _version)
	{
		if(!_v1.isObject())
			return _v1;
		Value firmware = Value::object();
		Value out = Value::object();
		for(const auto& [k, v] : _v1.asObject())
		{
			if(isIn(_layout.topLevel, k))
				firmware.set(k, v);
			else if(k == _layout.mergedObject && v.isObject())
				for(const auto& [mk, mv] : v.asObject())
					firmware.set(mk, mv);
			else if(k == "version")
				out.set(k, _version);
			else if(k == "tracks" && v.isArray() && !_layout.perTrack.empty())
			{
				Value tracks = Value::array();
				std::vector<Value> lists(_layout.perTrack.size(), Value::array());
				for(const auto& track : v.asArray())
				{
					Value t = track;
					for(size_t n = 0; n < _layout.perTrack.size(); ++n)
					{
						const auto [outer, inner] = split(_layout.perTrack[n].first);
						const auto* holder = t.find(outer);
						const auto* field = holder && !inner.empty() ? holder->find(inner) : holder;
						lists[n].push(field ? *field : Value());
						if(field && !inner.empty())
							t.put(outer, without(*holder, inner));
						else if(field)
							t = without(t, outer);
					}
					tracks.push(std::move(t));
				}
				out.set(k, std::move(tracks));
				for(size_t n = 0; n < _layout.perTrack.size(); ++n)
					firmware.set(_layout.perTrack[n].second, std::move(lists[n]));
			}
			else
				out.set(k, v);
		}
		out.set("firmware", std::move(firmware));
		return out;
	}

	Value ungroupFirmware(const Value& _doc, const FirmwareLayout& _layout, const int _version, const int _v1Version)
	{
		const auto* version = _doc.find("version");
		const auto* firmware = _doc.find("firmware");
		if(!_doc.isObject() || !version || !version->isNumber() || version->asNumber() != _version || !firmware || !firmware->isObject())
			return _doc;
		Value merged = Value::object();
		Value out = Value::object();
		for(const auto& [k, v] : _doc.asObject())
		{
			if(k == "firmware")
				continue;
			if(k == "version")
			{
				out.set(k, _v1Version);
				continue;
			}
			if(k == "tracks" && v.isArray() && !_layout.perTrack.empty())
			{
				Value tracks = Value::array();
				for(size_t i = 0; i < v.asArray().size(); ++i)
				{
					Value t = v.asArray()[i];
					for(const auto& [path, name] : _layout.perTrack)
					{
						const auto* list = firmware->find(name);
						if(!list || !list->isArray() || i >= list->asArray().size() || list->asArray()[i].isNull())
							continue;
						const auto [outer, inner] = split(path);
						if(inner.empty())
							t.put(outer, list->asArray()[i]);
						else if(auto* holder = t.find(outer); holder && holder->isObject())
							holder->put(inner, list->asArray()[i]);
					}
					tracks.push(std::move(t));
				}
				out.set(k, std::move(tracks));
				continue;
			}
			out.set(k, v);
		}
		for(const auto& [k, v] : firmware->asObject())
		{
			if(isIn(_layout.topLevel, k))
				out.set(k, v);
			else
			{
				bool perTrack = false;
				for(const auto& [path, name] : _layout.perTrack)
					perTrack = perTrack || name == k;
				if(!perTrack)
					merged.set(k, v);
			}
		}
		if(!_layout.mergedObject.empty())
			out.set(_layout.mergedObject, std::move(merged));
		return out;
	}
}

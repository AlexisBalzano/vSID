#include "pch.h"

#include "eseparser.h"
#include "flightplan.h"
#include "logger.h"

#include <format>

#include <thread>

void vsid::EseParser::parseEse(const std::filesystem::path& folder, const std::filesystem::path& file)
{
	std::filesystem::path fullPath = folder / file;
	std::string fileICAO = file.string().substr(0, 4);

	size_t currSidSize = this->tmpBuffer_.sectionSids.size();

	vsid::Logger::log(vsid::LogLevel::Debug, "Path to .ese for parsing: " + fullPath.string(), vsid::DebugLevel::Conf);

	std::ifstream in(fullPath, std::ios::binary | std::ios::ate);
	if (!in)
	{
		throw std::runtime_error("Failed to open .ese file in path: " + fullPath.string());
	}

	std::streamsize size = in.tellg();
	in.seekg(0, std::ios::beg);

	std::string fileContent(size, '\0');
	if (!in.read(fileContent.data(), size))
	{
		throw std::runtime_error("Failed to read .ese file content");
	}

	in.close();

	std::vector<std::string_view> lines = vsid::utils::splitSV(fileContent, '\n');

	Section currSection = Section::None;

	for(std::string_view l : lines)
	{
		l = vsid::utils::trimSV(l);
		if(l.empty() || l.front() == ';') continue;

		if (isHeader(l)) // set new section and leave old one if active
		{
			if (currSection != Section::None) exit(currSection);

			currSection = toSection(l);
			enter(currSection);
			continue;
		}

		if (currSection != Section::None) // work through non-empty and non-comment lines
		{
			line(currSection, l);
		}
	}

	if (currSection != Section::None) exit(currSection);

	size_t sidAdded = this->tmpBuffer_.sectionSids.size() - currSidSize;

	vsid::Logger::log(LogLevel::Debug, std::format("Parsed [{}] - Added [{}] SIDs", fileICAO, sidAdded), DebugLevel::Ese);
}

void vsid::EseParser::enter(vsid::EseParser::Section s)
{
	switch (s)
	{
	case Section::SidsStars:
		vsid::Logger::log(LogLevel::Debug, "Started parsing of Sids/Stars.", DebugLevel::Ese);
		break;

	case Section::Unknown:
	case Section::None:
		break;
	}
}

void vsid::EseParser::line(Section s, std::string_view l)
{
	switch (s)
	{
	case Section::SidsStars:
	{
		std::vector<std::string_view> sidVec = vsid::utils::splitSV(l, ':');

		try
		{
			if (sidVec.empty() || sidVec.at(0) == "STAR") break; // only parse SIDs

			std::string_view currSid = vsid::utils::trimSV(sidVec.at(3));

			if (currSid.length() < 3) break; // protection for num / desig extraction

			auto [sid, trans] = vsid::fplnhelper::splitTransitionSV(currSid);

			vsid::Logger::log(vsid::LogLevel::Debug, std::format("Parsing SID [{}] - sid: [{}] / trans : [{}]", currSid, sid, trans), vsid::DebugLevel::Ese, true);

			vsid::SectionTransition sectionTrans("", std::nullopt, std::nullopt);
			vsid::SectionSID sectionSid("", "", '\0', std::nullopt, "", sectionTrans, "");

			if (!sid.empty())
			{
				sectionSid.apt = vsid::utils::trimSV(sidVec.at(1));
				sectionSid.rwy = vsid::utils::trimSV(sidVec.at(2));
				if(sidVec.size() > 4) sectionSid.route = vsid::utils::trimSV(sidVec.at(4));

				if (vsid::utils::lastIsDigit(sid))
				{
					sectionSid.base = sid.substr(0, sid.length() - 1);
					sectionSid.number = sid.back();
				}
				else if (sid.length() > 2)
				{
					if (vsid::utils::lastIsDigit(sid.substr(0, sid.length() - 1)))
					{
						sectionSid.base = sid.substr(0, sid.length() - 2);
						sectionSid.number = sid[sid.length() - 2];
						sectionSid.desig = sid.back();
					}
					else sectionSid.base = sid;
				}
			}
			else break;

			if (!trans.empty())
			{
				if (vsid::utils::lastIsDigit(trans))
				{
					sectionTrans.base = trans.substr(0, trans.length() - 1);
					sectionTrans.number = trans.back();
				}
				else if(trans.length() > 2)
				{
					if (vsid::utils::lastIsDigit(trans.substr(0, trans.length() - 1)))
					{
						sectionTrans.base = trans.substr(0, trans.length() - 2);
						sectionTrans.number = trans[trans.length() - 2];
						sectionTrans.desig = trans.back();
					}
					else sectionTrans.base = trans;
				}
			}

			sectionSid.trans = std::move(sectionTrans);

			vsid::Logger::log(vsid::LogLevel::Debug, std::format("Stored SID [{}{}{}] and transition: [{}{}{}]",
				sectionSid.base, sectionSid.number, (sectionSid.desig) ? std::string(1, *sectionSid.desig) : "",
				sectionSid.trans.base, (sectionSid.trans.number) ? std::string(1, *sectionSid.trans.number) : "",
				(sectionSid.trans.desig) ? std::string(1, *sectionSid.trans.desig) : ""),
				vsid::DebugLevel::Ese, true);

			this->tmpBuffer_.sectionSids.emplace(std::move(sectionSid));
		}
		catch (const std::out_of_range& e)
		{
			vsid::Logger::log(vsid::LogLevel::Error, std::format("Failed to parse SID [{}] in .ese line [{}]", e.what(), l));
		}

		break;
	}
	case Section::Unknown:
	case Section::None:
		break;
	}
}

void vsid::EseParser::exit(Section s)
{
	switch (s)
	{
	case Section::SidsStars:
		break;

	case Section::Unknown:
	case Section::None:
		break;
	}
}

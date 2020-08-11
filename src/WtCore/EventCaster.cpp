/*!
 * \file EventCaster.cpp
 * \project	WonderTrader
 *
 * \author Wesley
 * \date 2020/03/30
 * 
 * \brief 
 */
#include "EventCaster.h"

#include "../Share/StrUtil.hpp"
#include "../Includes/WTSTradeDef.hpp"
#include "../Includes/WTSCollection.hpp"
#include "../Includes/WTSContractInfo.hpp"
#include "../Includes/WTSVariant.hpp"

#include "../WTSTools/WTSBaseDataMgr.h"
#include "../WTSTools/WTSLogger.h"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
namespace rj = rapidjson;

USING_NS_OTP;

#pragma warning(disable:4200)


#define UDP_MSG_PUSHTRADE	0x300
#define UDP_MSG_PUSHORDER	0x301

#pragma pack(push,1)
//UDP�����
typedef struct _UDPPacket
{
	uint32_t		_type;
	uint32_t		_length;
	char			_data[0];
} UDPPacket;
#pragma pack(pop)

EventCaster::EventCaster()
	: m_bTerminated(false)
{
	
}


EventCaster::~EventCaster()
{
}

bool EventCaster::init(WTSVariant* cfg)
{
	if (!cfg->getBoolean("active"))
		return false;

	WTSVariant* cfgBC = cfg->get("broadcast");
	if (cfgBC)
	{
		for (uint32_t idx = 0; idx < cfgBC->size(); idx++)
		{
			WTSVariant* cfgItem = cfgBC->get(idx);
			addBRecver(cfgItem->getCString("host"), cfgItem->getInt32("port"));
		}
	}

	WTSVariant* cfgMC = cfg->get("multicast");
	if (cfgMC)
	{
		for (uint32_t idx = 0; idx < cfgMC->size(); idx++)
		{
			WTSVariant* cfgItem = cfgMC->get(idx);
			addMRecver(cfgItem->getCString("host"), cfgItem->getInt32("port"), cfgItem->getInt32("sendport"));
		}
	}

	start(cfg->getInt32("bport"));

	return true;
}

void EventCaster::start(int bport)
{
	if (!m_listRawRecver.empty())
	{
		m_sktBroadcast.reset(new UDPSocket(m_ioservice, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0)));
		boost::asio::socket_base::broadcast option(true);
		m_sktBroadcast->set_option(option);
	}

	m_thrdIO.reset(new BoostThread([this](){
		try
		{
			m_ioservice.run();
		}
		catch(...)
		{
			m_ioservice.stop();
		}
	}));

	WTSLogger::info("�¼��㲥��������");
}

void EventCaster::stop()
{
	m_bTerminated = true;
	m_ioservice.stop();
	if (m_thrdIO)
		m_thrdIO->join();

	m_condCast.notify_all();
	if (m_thrdCast)
		m_thrdCast->join();
}

bool EventCaster::addBRecver(const char* remote, int port)
{
	try
	{
		boost::asio::ip::address_v4 addr = boost::asio::ip::address_v4::from_string(remote);
		m_listRawRecver.push_back(EndPoint(addr, port));
	}
	catch(...)
	{
		return false;
	}

	return true;
}


bool EventCaster::addMRecver(const char* remote, int port, int sendport)
{
	try
	{
		boost::asio::ip::address_v4 addr = boost::asio::ip::address_v4::from_string(remote);
		auto ep = EndPoint(addr, port);
		UDPSocketPtr sock(new UDPSocket(m_ioservice, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), sendport)));
		boost::asio::ip::multicast::join_group option(ep.address());
		sock->set_option(option);
		m_listRawGroup.push_back(std::make_pair(sock, ep));
	}
	catch(...)
	{
		return false;
	}

	return true;
}

void EventCaster::broadcast(const char* trader, uint32_t localid, const char* stdCode, WTSTradeInfo* trdInfo)
{
	broadcast(trader, localid, stdCode, trdInfo, UDP_MSG_PUSHTRADE);
}

void EventCaster::broadcast(const char* trader, uint32_t localid, const char* stdCode, WTSOrderInfo* ordInfo)
{
	broadcast(trader, localid, stdCode, ordInfo, UDP_MSG_PUSHORDER);
}

void EventCaster::tradeToJson(uint32_t localid, const char* stdCode, WTSTradeInfo* trdInfo, std::string& output)
{
	if(trdInfo == NULL)
	{
		output = "{}";
		return;
	}

	bool isLong = (trdInfo->getDirection() == WDT_LONG);
	bool isOpen = (trdInfo->getOffsetType() == WOT_OPEN);
	bool isToday = (trdInfo->getOffsetType() == WOT_CLOSETODAY);

	{
		rj::Document root(rj::kObjectType);
		rj::Document::AllocatorType &allocator = root.GetAllocator();

		root.AddMember("localid", localid, allocator);
		root.AddMember("code", rj::Value(stdCode, allocator), allocator);
		root.AddMember("islong", isLong, allocator);
		root.AddMember("isopen", isOpen, allocator);
		root.AddMember("istoday", isToday, allocator);

		root.AddMember("volumn", trdInfo->getVolumn(), allocator);
		root.AddMember("price", trdInfo->getPrice(), allocator);

		rj::StringBuffer sb;
		rj::PrettyWriter<rj::StringBuffer> writer(sb);
		root.Accept(writer);

		output = sb.GetString();
	}
}

void EventCaster::orderToJson(uint32_t localid, const char* stdCode, WTSOrderInfo* ordInfo, std::string& output)
{
	if (ordInfo == NULL)
	{
		output = "{}";
		return;
	}

	bool isLong = (ordInfo->getDirection() == WDT_LONG);
	bool isOpen = (ordInfo->getOffsetType() == WOT_OPEN);
	bool isToday = (ordInfo->getOffsetType() == WOT_CLOSETODAY);
	bool isCanceled = (ordInfo->getOrderState() == WOS_Canceled);

	{
		rj::Document root(rj::kObjectType);
		rj::Document::AllocatorType &allocator = root.GetAllocator();

		root.AddMember("localid", localid, allocator);
		root.AddMember("code", rj::Value(stdCode, allocator), allocator);
		root.AddMember("islong", isLong, allocator);
		root.AddMember("isopen", isOpen, allocator);
		root.AddMember("istoday", isToday, allocator);
		root.AddMember("canceled", isCanceled, allocator);

		root.AddMember("total", ordInfo->getVolumn(), allocator);
		root.AddMember("left", ordInfo->getVolLeft(), allocator);
		root.AddMember("traded", ordInfo->getVolTraded(), allocator);
		root.AddMember("price", ordInfo->getPrice(), allocator);
		root.AddMember("state", rj::Value(ordInfo->getStateMsg(), allocator), allocator);

		rj::StringBuffer sb;
		rj::PrettyWriter<rj::StringBuffer> writer(sb);
		root.Accept(writer);

		output = sb.GetString();
	}
}

void EventCaster::broadcast(const char* trader, uint32_t localid, const char* stdCode, WTSObject* data, uint32_t dataType)
{
	if(m_sktBroadcast == NULL || data == NULL || m_bTerminated)
		return;

	{
		BoostUniqueLock lock(m_mtxCast);
		m_dataQue.push(CastData(trader, localid, stdCode, data, dataType));
	}

	if(m_thrdCast == NULL)
	{
		m_thrdCast.reset(new BoostThread([this](){

			while (!m_bTerminated)
			{
				if(m_dataQue.empty())
				{
					BoostUniqueLock lock(m_mtxCast);
					m_condCast.wait(lock);
					continue;
				}	

				std::queue<CastData> tmpQue;
				{
					BoostUniqueLock lock(m_mtxCast);
					tmpQue.swap(m_dataQue);
				}
				
				while(!tmpQue.empty())
				{
					const CastData& castData = tmpQue.front();

					if (castData._data == NULL)
						break;

					//ֱ�ӹ㲥
					if (!m_listRawGroup.empty() || !m_listRawRecver.empty())
					{
						std::string buf_raw;
						std::string data;
						if (castData._datatype == UDP_MSG_PUSHTRADE)
						{
							tradeToJson(castData._localid, castData._code, (WTSTradeInfo*)castData._data, data);
						}
						else if(castData._datatype == UDP_MSG_PUSHORDER)
						{
							orderToJson(castData._localid, castData._code, (WTSOrderInfo*)castData._data, data);
						}
						buf_raw.resize(sizeof(UDPPacket)+data.size());
						UDPPacket* pack = (UDPPacket*)buf_raw.data();
						pack->_length = data.size();
						pack->_type = castData._datatype;
						memcpy(&pack->_data, data.data(), data.size());

						//�㲥
						for (auto it = m_listRawRecver.begin(); it != m_listRawRecver.end(); it++)
						{
							const EndPoint& receiver = (*it);
							m_sktBroadcast->send_to(boost::asio::buffer(buf_raw), receiver);
						}

						//�鲥
						for (auto it = m_listRawGroup.begin(); it != m_listRawGroup.end(); it++)
						{
							const MulticastPair& item = *it;
							it->first->send_to(boost::asio::buffer(buf_raw), item.second);
						}
					}

					tmpQue.pop();
				} 
			}
		}));
	}
	else
	{
		m_condCast.notify_all();
	}
}

void EventCaster::handle_send_broad(const EndPoint& ep, const boost::system::error_code& error, std::size_t bytes_transferred)
{
	if(error)
	{
		WTSLogger::error("�¼��㲥ʧ�ܣ�Ŀ���ַ��%s��������Ϣ��%s", ep.address().to_string().c_str(), error.message().c_str());
	}
}

void EventCaster::handle_send_multi(const EndPoint& ep, const boost::system::error_code& error, std::size_t bytes_transferred)
{
	if(error)
	{
		WTSLogger::error("�¼��ಥʧ�ܣ�Ŀ���ַ��%s��������Ϣ��%s", ep.address().to_string().c_str(), error.message().c_str());
	}
}

;
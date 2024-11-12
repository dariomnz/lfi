
// /*
//  *  Copyright 2024-2025 Dario Muñoz Muñoz, Felix Garcia Carballeira, Diego Camarmas Alonso, Alejandro Calderon Mateos
//  *
//  *  This file is part of LFI.
//  *
//  *  LFI is free software: you can redistribute it and/or modify
//  *  it under the terms of the GNU Lesser General Public License as published by
//  *  the Free Software Foundation, either version 3 of the License, or
//  *  (at your option) any later version.
//  *
//  *  LFI is distributed in the hope that it will be useful,
//  *  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  *  GNU Lesser General Public License for more details.
//  *
//  *  You should have received a copy of the GNU Lesser General Public License
//  *  along with LFI.  If not, see <http://www.gnu.org/licenses/>.
//  *
//  */

// #define DEBUG
// #include "impl/fabric.hpp"
// #include "impl/debug.hpp"
// #include "impl/env.hpp"

// namespace LFI
// {

// std::mutex fabric::s_mutex;

// void print_fi_cq_tagged_entry(const fi_cq_tagged_entry &entry)
// {
// 	debug_info("fi_cq_tagged_entry:");
// 	debug_info("  op_context: " << entry.op_context);
// 	debug_info("  Flags set:");
//     if (entry.flags & FI_MSG) { debug_info("    FI_MSG"); }
//     if (entry.flags & FI_RMA) { debug_info("    FI_RMA"); }
//     if (entry.flags & FI_TAGGED) { debug_info("    FI_TAGGED"); }
//     if (entry.flags & FI_ATOMIC) { debug_info("    FI_ATOMIC"); }
//     if (entry.flags & FI_MULTICAST) { debug_info("    FI_MULTICAST"); }
//     if (entry.flags & FI_COLLECTIVE) { debug_info("    FI_COLLECTIVE"); }
    
//     if (entry.flags & FI_READ) { debug_info("    FI_READ"); }
//     if (entry.flags & FI_WRITE) { debug_info("    FI_WRITE"); }
//     if (entry.flags & FI_RECV) { debug_info("    FI_RECV"); }
//     if (entry.flags & FI_SEND) { debug_info("    FI_SEND"); }
//     if (entry.flags & FI_REMOTE_READ) { debug_info("    FI_REMOTE_READ"); }
//     if (entry.flags & FI_REMOTE_WRITE) { debug_info("    FI_REMOTE_WRITE"); }
    
//     if (entry.flags & FI_MULTI_RECV) { debug_info("    FI_MULTI_RECV"); }
//     if (entry.flags & FI_REMOTE_CQ_DATA) { debug_info("    FI_REMOTE_CQ_DATA"); }
//     if (entry.flags & FI_MORE) { debug_info("    FI_MORE"); }
//     if (entry.flags & FI_PEEK) { debug_info("    FI_PEEK"); }
//     if (entry.flags & FI_TRIGGER) { debug_info("    FI_TRIGGER"); }
//     if (entry.flags & FI_FENCE) { debug_info("    FI_FENCE"); }
//     // if (entry.flags & FI_PRIORITY) { debug_info("    FI_PRIORITY"); }

//     if (entry.flags & FI_COMPLETION) { debug_info("    FI_COMPLETION"); }
//     if (entry.flags & FI_INJECT) { debug_info("    FI_INJECT"); }
//     if (entry.flags & FI_INJECT_COMPLETE) { debug_info("    FI_INJECT_COMPLETE"); }
//     if (entry.flags & FI_TRANSMIT_COMPLETE) { debug_info("    FI_TRANSMIT_COMPLETE"); }
//     if (entry.flags & FI_DELIVERY_COMPLETE) { debug_info("    FI_DELIVERY_COMPLETE"); }
//     if (entry.flags & FI_AFFINITY) { debug_info("    FI_AFFINITY"); }
//     if (entry.flags & FI_COMMIT_COMPLETE) { debug_info("    FI_COMMIT_COMPLETE"); }
//     if (entry.flags & FI_MATCH_COMPLETE) { debug_info("    FI_MATCH_COMPLETE"); }

//     if (entry.flags & FI_HMEM) { debug_info("    FI_HMEM"); }
//     if (entry.flags & FI_VARIABLE_MSG) { debug_info("    FI_VARIABLE_MSG"); }
//     if (entry.flags & FI_RMA_PMEM) { debug_info("    FI_RMA_PMEM"); }
//     if (entry.flags & FI_SOURCE_ERR) { debug_info("    FI_SOURCE_ERR"); }
//     if (entry.flags & FI_LOCAL_COMM) { debug_info("    FI_LOCAL_COMM"); }
//     if (entry.flags & FI_REMOTE_COMM) { debug_info("    FI_REMOTE_COMM"); }
//     if (entry.flags & FI_SHARED_AV) { debug_info("    FI_SHARED_AV"); }
//     if (entry.flags & FI_PROV_ATTR_ONLY) { debug_info("    FI_PROV_ATTR_ONLY"); }
//     if (entry.flags & FI_NUMERICHOST) { debug_info("    FI_NUMERICHOST"); }
//     if (entry.flags & FI_RMA_EVENT) { debug_info("    FI_RMA_EVENT"); }
//     if (entry.flags & FI_SOURCE) { debug_info("    FI_SOURCE"); }
//     if (entry.flags & FI_NAMED_RX_CTX) { debug_info("    FI_NAMED_RX_CTX"); }
//     if (entry.flags & FI_DIRECTED_RECV) { debug_info("    FI_DIRECTED_RECV"); }
// 	debug_info("  len: " << entry.len);
// 	debug_info("  buf: " << entry.buf);
// 	debug_info("  data: " << entry.data);
// 	debug_info("  tag: " << entry.tag);
// }

// int fabric::set_hints(fabric_ep &fabric_ep)
// {
// 	fabric_ep.hints = fi_allocinfo();
// 	if (!fabric_ep.hints)
// 		return -FI_ENOMEM;

// 	/*
// 	 * Request FI_EP_RDM (reliable datagram) endpoint which will allow us
// 	 * to reliably send messages to peers without having to
// 	 * listen/connect/accept.
// 	 */
// 	fabric_ep.hints->ep_attr->type = FI_EP_RDM;

// 	/*
// 	 * Request basic messaging capabilities from the provider (no tag
// 	 * matching, no RMA, no atomic operations)
// 	 */
// 	fabric_ep.hints->caps = FI_MSG | FI_TAGGED;

// 	/*
// 	 * Default to FI_DELIVERY_COMPLETE which will make sure completions do
// 	 * not get generated until our message arrives at the destination.
// 	 * Otherwise, the client might get a completion and exit before the
// 	 * server receives the message. This is to make the test simpler.
// 	 */
// 	fabric_ep.hints->tx_attr->op_flags = FI_DELIVERY_COMPLETE;

// 	fabric_ep.hints->mode = FI_CONTEXT;

// 	fabric_ep.hints->domain_attr->threading = FI_THREAD_SAFE;

// 	fabric_ep.hints->fabric_attr->name = "sm2";
// 	/* Done setting hints */

// 	return 0;
// }

// int fabric::init(fabric_ep &fabric_ep, bool have_threads)
// {
// 	int ret;
// 	struct fi_cq_attr cq_attr = {};
// 	struct fi_av_attr av_attr = {};

// 	debug_info("[FABRIC] [fabric_init] Start");

// 	std::unique_lock<std::mutex> lock(s_mutex);

// 	fabric_ep.have_thread = have_threads;

// 	set_hints(fabric_ep);

// 	ret = fi_getinfo(fi_version(), NULL, NULL, 0,
// 					 fabric_ep.hints, &fabric_ep.info);

// 	debug_info("[FABRIC] [fabric_init] fi_getinfo = " << ret);
// 	if (ret)
// 	{
// 		printf("fi_getinfo error (%d)\n", ret);
// 		return ret;
// 	}

// 	debug_info("[FABRIC] [fabric_init] " << fi_tostr(fabric_ep.info, FI_TYPE_INFO));

// 	ret = fi_fabric(fabric_ep.info->fabric_attr, &fabric_ep.fabric, NULL);
// 	debug_info("[FABRIC] [fabric_init] fi_fabric = " << ret);
// 	if (ret)
// 	{
// 		printf("fi_fabric error (%d)\n", ret);
// 		return ret;
// 	}

// 	ret = fi_domain(fabric_ep.fabric, fabric_ep.info, &fabric_ep.domain, NULL);
// 	debug_info("[FABRIC] [fabric_init] fi_domain = " << ret);
// 	if (ret)
// 	{
// 		printf("fi_domain error (%d)\n", ret);
// 		return ret;
// 	}

// 	cq_attr.format = FI_CQ_FORMAT_TAGGED;
// 	cq_attr.wait_obj = FI_WAIT_NONE;
// 	ret = fi_cq_open(fabric_ep.domain, &cq_attr, &fabric_ep.cq, NULL);
// 	debug_info("[FABRIC] [fabric_init] fi_cq_open = " << ret);
// 	if (ret)
// 	{
// 		printf("fi_cq_open error (%d)\n", ret);
// 		return ret;
// 	}

// 	av_attr.type = FI_AV_MAP;
// 	ret = fi_av_open(fabric_ep.domain, &av_attr, &fabric_ep.av, NULL);
// 	debug_info("[FABRIC] [fabric_init] fi_av_open = " << ret);
// 	if (ret)
// 	{
// 		printf("fi_av_open error (%d)\n", ret);
// 		return ret;
// 	}

// 	// Try opening a scalable endpoint if it is not posible a normal endpoint
// 	ret = fi_scalable_ep(fabric_ep.domain, fabric_ep.info, &fabric_ep.ep, NULL);
// 	debug_info("[FABRIC] [fabric_init] fi_scalable_ep = " << ret);
// 	if (ret == -FI_ENOSYS)
// 	{
// 		fabric_ep.use_scalable_ep = false;
// 	}
// 	else if (ret)
// 	{
// 		printf("fi_scalable_ep error (%d)\n", ret);
// 		return ret;
// 	}

// 	if (fabric_ep.use_scalable_ep)
// 	{
// 		ret = fi_scalable_ep_bind(fabric_ep.ep, &fabric_ep.av->fid, 0);
// 		debug_info("[FABRIC] [fabric_init] fi_scalable_ep_bind = " << ret);
// 		if (ret)
// 		{
// 			printf("fi_scalable_ep_bind av error (%d)\n", ret);
// 			return ret;
// 		}

// 		ret = fi_enable(fabric_ep.ep);
// 		debug_info("[FABRIC] [fabric_init] fi_enable = " << ret);
// 		if (ret)
// 		{
// 			printf("fi_enable error (%d)\n", ret);
// 			return ret;
// 		}

// 		fabric_ep.info->tx_attr->caps |= FI_MSG;
// 		fabric_ep.info->tx_attr->caps |= FI_NAMED_RX_CTX; /* Required for scalable endpoints indexing */
// 		ret = fi_tx_context(fabric_ep.ep, 0, fabric_ep.info->tx_attr, &fabric_ep.tx_ep, NULL);
// 		debug_info("[FABRIC] [fabric_init] fi_tx_context tx_ep = " << ret);
// 		if (ret)
// 		{
// 			printf("fi_tx_context error (%d)\n", ret);
// 			return ret;
// 		}

// 		ret = fi_ep_bind(fabric_ep.tx_ep, &fabric_ep.cq->fid, FI_SEND);
// 		debug_info("[FABRIC] [fabric_init] fi_ep_bind tx_ep = " << ret);
// 		if (ret)
// 		{
// 			printf("fi_ep_bind error (%d)\n", ret);
// 			return ret;
// 		}

// 		ret = fi_enable(fabric_ep.tx_ep);
// 		debug_info("[FABRIC] [fabric_init] fi_enable tx_ep = " << ret);
// 		if (ret)
// 		{
// 			printf("fi_enable error (%d)\n", ret);
// 			return ret;
// 		}

// 		fabric_ep.info->rx_attr->caps |= FI_MSG;
// 		fabric_ep.info->rx_attr->caps |= FI_NAMED_RX_CTX; /* Required for scalable endpoints indexing */
// 		ret = fi_rx_context(fabric_ep.ep, 0, fabric_ep.info->rx_attr, &fabric_ep.rx_ep, NULL);
// 		debug_info("[FABRIC] [fabric_init] fi_rx_context rx_ep = " << ret);
// 		if (ret)
// 		{
// 			printf("fi_rx_context error (%d)\n", ret);
// 			return ret;
// 		}

// 		ret = fi_ep_bind(fabric_ep.rx_ep, &fabric_ep.cq->fid, FI_RECV);
// 		debug_info("[FABRIC] [fabric_init] fi_ep_bind rx_ep = " << ret);
// 		if (ret)
// 		{
// 			printf("fi_ep_bind error (%d)\n", ret);
// 			return ret;
// 		}

// 		ret = fi_enable(fabric_ep.rx_ep);
// 		debug_info("[FABRIC] [fabric_init] fi_enable rx_ep = " << ret);
// 		if (ret)
// 		{
// 			printf("fi_enable error (%d)\n", ret);
// 			return ret;
// 		}
// 	}
// 	else
// 	{

// 		ret = fi_endpoint(fabric_ep.domain, fabric_ep.info, &fabric_ep.ep, NULL);
// 		debug_info("[FABRIC] [fabric_init] fi_endpoint = " << ret);
// 		if (ret)
// 		{
// 			printf("fi_endpoint error (%d)\n", ret);
// 			return ret;
// 		}

// 		ret = fi_ep_bind(fabric_ep.ep, &fabric_ep.av->fid, 0);
// 		debug_info("[FABRIC] [fabric_init] fi_ep_bind = " << ret);
// 		if (ret)
// 		{
// 			printf("fi_ep_bind error (%d)\n", ret);
// 			return ret;
// 		}

// 		ret = fi_ep_bind(fabric_ep.ep, &fabric_ep.cq->fid, FI_SEND | FI_RECV);
// 		debug_info("[FABRIC] [fabric_init] fi_ep_bind = " << ret);
// 		if (ret)
// 		{
// 			printf("fi_ep_bind error (%d)\n", ret);
// 			return ret;
// 		}

// 		ret = fi_enable(fabric_ep.ep);
// 		debug_info("[FABRIC] [fabric_init] fi_enable = " << ret);
// 		if (ret)
// 		{
// 			printf("fi_enable error (%d)\n", ret);
// 			return ret;
// 		}
// 	}

// 	// Create FABRIC_ANY_RANK
// 	fabric::any_comm(fabric_ep);

// 	fabric::init_thread_cq(fabric_ep);

// 	return 0;
// }

// int fabric::init_thread_cq(fabric_ep &fabric_ep)
// {
// 	if (!fabric_ep.have_thread)
// 		return 0;

// 	debug_info("[FABRIC] [init_thread_cq] Start");
// 	fabric_ep.threads_cq = std::vector<fabric_ep::thread_cq>(env::get_instance().LFI_threads);

// 	for (size_t i = 0; i < fabric_ep.threads_cq.size(); i++)
// 	{
// 		fabric_ep.threads_cq[i].id = std::thread([&fabric_ep, i]()
// 												 { run_thread_cq(fabric_ep, i); });
// 	}
// 	debug_info("[FABRIC] [init_thread_cq] End");
// 	return 0;
// }

// int fabric::destroy_thread_cq(fabric_ep &fabric_ep)
// {
// 	if (!fabric_ep.have_thread)
// 		return 0;

// 	debug_info("[FABRIC] [destroy_thread_cq] Start");

// 	for (size_t i = 0; i < fabric_ep.threads_cq.size(); i++)
// 	{
// 		auto &t = fabric_ep.threads_cq[i];
// 		{
// 			std::lock_guard<std::mutex> lock(t.thread_cq_mutex);
// 			t.thread_cq_is_running = false;
// 		}
// 		t.thread_cq_cv.notify_one();
// 		t.id.join();
// 	}

// 	fabric_ep.threads_cq.clear();

// 	debug_info("[FABRIC] [destroy_thread_cq] End");
// 	return 0;
// }

// int fabric::run_thread_cq(fabric_ep &fabric_ep, uint32_t id)
// {
// 	int ret = 0;
// 	const int comp_count = 8;
// 	struct fi_cq_tagged_entry comp[comp_count] = {};
// 	auto &t = fabric_ep.threads_cq[id];
// 	std::unique_lock<std::mutex> lock(t.thread_cq_mutex);

// 	while (t.thread_cq_is_running)
// 	{
// 		if (t.thread_cq_cv.wait_for(lock, std::chrono::nanoseconds(1), [&t]
// 									{ return !t.thread_cq_is_running; }))
// 		{
// 			break;
// 		}
// 		// if (fabric_ep.subs_to_wait == 0) {
// 		// 	fabric_ep.thread_cq_cv.wait(lock, [&fabric_ep]{ return fabric_ep.subs_to_wait != 0 || !fabric_ep.thread_cq_is_running; });
// 		// }
// 		// if (!fabric_ep.thread_cq_is_running) break;
// 		// if (fabric_ep.thread_cq_cv.wait_for(lock, std::chrono::nanoseconds(1), [&fabric_ep]{ return !fabric_ep.thread_cq_is_running; })) {
// 		// 	break;
// 		// }

// 		// if (fabric_ep.subs_to_wait == 0) { continue; }
// 		// {
// 		// std::unique_lock<std::mutex> lock(fabric_ep.thread_fi_mutex);
// 		ret = fi_cq_read(fabric_ep.cq, comp, comp_count);
// 		// ret = fi_cq_read(fabric_ep.cq, &comp[0], 8);
// 		// }
// 		if (ret == -FI_EAGAIN)
// 		{
// 			continue;
// 		}

// 		// TODO: handle error
// 		if (ret < 0)
// 		{
// 			continue;
// 		}

// 		// Handle the cq entries
// 		for (int i = 0; i < ret; i++)
// 		{
// 			fabric_context *context = static_cast<fabric_context *>(comp[i].op_context);
// 			fabric_comm &comm = fabric_ep.m_comms[context->rank];
// 			context->entry = comp[i];

// 			{
// 				std::unique_lock<std::mutex> lock(comm.comm_mutex);
// 				if (comp[i].flags & FI_SEND)
// 				{
// 					debug_info("[FABRIC] [run_thread_cq] Send cq of rank_peer " << context->rank);
// 				}
// 				if (comp[i].flags & FI_RECV)
// 				{
// 					debug_info("[FABRIC] [run_thread_cq] Recv cq of rank_peer " << context->rank);
// 				}

// 				// print_fi_cq_err_entry(comp);
// 				// fabric_ep.subs_to_wait--;
// 				comm.wait_context = false;
// 				comm.comm_cv.notify_one();
// 			}
// 		}
// 	}
// 	return ret;
// }

// fabric::fabric_comm &fabric::new_comm(fabric_ep &fabric_ep)
// {
// 	static uint32_t rank_counter = 0;

// 	debug_info("[FABRIC] [fabric_new_comm] Start");
// 	std::unique_lock<std::mutex> lock(s_mutex);

// 	auto [key, inserted] = fabric_ep.m_comms.emplace(std::piecewise_construct,
// 													 std::forward_as_tuple(rank_counter),
// 													 std::forward_as_tuple());
// 	key->second.m_ep = &fabric_ep;
// 	key->second.rank_peer = rank_counter;
// 	rank_counter++;
// 	debug_info("[FABRIC] [fabric_new_comm] rank_peer " << key->second.rank_peer);
// 	debug_info("[FABRIC] [fabric_new_comm] End");
// 	return key->second;
// }

// fabric::fabric_comm &fabric::any_comm(fabric_ep &fabric_ep)
// {
// 	debug_info("[FABRIC] [any_comm] Start");
// 	// std::unique_lock<std::mutex> lock(s_mutex);

// 	auto [key, inserted] = fabric_ep.m_comms.emplace(std::piecewise_construct,
// 													 std::forward_as_tuple(FABRIC_ANY_RANK),
// 													 std::forward_as_tuple());
// 	key->second.m_ep = &fabric_ep;
// 	key->second.rank_peer = FABRIC_ANY_RANK;
// 	key->second.fi_addr = FI_ADDR_UNSPEC;
// 	debug_info("[FABRIC] [any_comm] End");
// 	return key->second;
// }

// fabric::fabric_comm &fabric::get_any_rank_comm(fabric_ep &fabric_ep)
// {
// 	return fabric_ep.m_comms[FABRIC_ANY_RANK];
// }

// int fabric::get_addr(fabric_ep &fabric_ep, std::vector<uint8_t> &out_addr)
// {
// 	int ret = -1;
// 	debug_info("[FABRIC] [fabric_get_addr] Start");

// 	size_t size_addr = 0;
// 	ret = fi_getname(&fabric_ep.ep->fid, out_addr.data(), &size_addr);
// 	if (ret != -FI_ETOOSMALL)
// 	{
// 		printf("fi_getname error %d\n", ret);
// 		return ret;
// 	}
// 	debug_info("[FABRIC] [fabric_get_addr] size_addr " << size_addr);
// 	out_addr.resize(size_addr);
// 	ret = fi_getname(&fabric_ep.ep->fid, out_addr.data(), &size_addr);
// 	if (ret)
// 	{
// 		printf("fi_getname error %d\n", ret);
// 		return ret;
// 	}
// 	debug_info("[FABRIC] [fabric_get_addr] End = " << ret);
// 	return ret;
// }

// int fabric::register_addr(fabric_ep &fabric_ep, fabric_comm &fabric_comm, std::vector<uint8_t> &addr_buf)
// {
// 	int ret = -1;
// 	fi_addr_t fi_addr;
// 	debug_info("[FABRIC] [fabric_register_addr] Start");
// 	ret = fi_av_insert(fabric_ep.av, addr_buf.data(), 1, &fi_addr, 0, NULL);
// 	if (ret != 1)
// 	{
// 		printf("av insert error %d\n", ret);
// 		return ret;
// 	}

// 	fabric_comm.fi_addr = fi_addr;

// 	debug_info("[FABRIC] [fabric_register_addr] End = " << ret);
// 	return ret;
// }

// int fabric::remove_addr(fabric_ep &fabric_ep, fabric_comm &fabric_comm)
// {
// 	int ret = -1;
// 	debug_info("[FABRIC] [fabric_remove_addr] Start");
// 	ret = fi_av_remove(fabric_ep.av, &fabric_comm.fi_addr, 1, 0);
// 	if (ret < 0)
// 	{
// 		printf("av remove error %d\n", ret);
// 		return ret;
// 	}
// 	debug_info("[FABRIC] [fabric_remove_addr] End = " << ret);
// 	return ret;
// }

// void fabric::wait(fabric_ep &fabric_ep, fabric_comm &fabric_comm)
// {
// 	if (fabric_ep.have_thread)
// 	{
// 		debug_info("[FABRIC] [wait] With threads");
// 		std::unique_lock<std::mutex> lock(fabric_comm.comm_mutex);
// 		fabric_comm.comm_cv.wait(lock, [&fabric_comm]
// 								 { return !fabric_comm.wait_context; });
// 		fabric_comm.wait_context = true;
// 	}
// 	else
// 	{
// 		debug_info("[FABRIC] [wait] Without threads");
// 		std::unique_lock<std::mutex> lock(fabric_comm.comm_mutex);

// 		int ret = 0;
// 		const int comp_count = 8;
// 		fi_cq_tagged_entry comp[comp_count] = {};
// 		while (fabric_comm.wait_context)
// 		{
// 			ret = fi_cq_read(fabric_ep.cq, &comp, comp_count);

// 			if (ret == -FI_EAGAIN)
// 			{
// 				// std::this_thread::yield();
// 				continue;
// 			}

// 			// TODO: handle error
// 			if (ret < 0)
// 			{
// 				print("Error in fi_cq_read " << ret << " " << fi_strerror(ret));
// 				continue;
// 			}

// 			for (int i = 0; i < ret; i++)
// 			{
// 				// Handle the cq entries
// 				fabric_context *context = static_cast<fabric_context *>(comp[i].op_context);
// 				context->entry = comp[i];
// 				if (comp[i].flags & FI_SEND)
// 				{
// 					debug_info("[FABRIC] [wait] Send cq of rank_peer " << context->rank);
// 				}
// 				if (comp[i].flags & FI_RECV)
// 				{
// 					debug_info("[FABRIC] [wait] Recv cq of rank_peer " << context->rank);
// 				}
// 				print_fi_cq_tagged_entry(comp[i]);
// 				fabric_ep.m_comms[context->rank].wait_context = false;
// 				// fabric_ep.m_comms[context->rank].comm_cv.notify_one();
// 			}
// 		}
// 		fabric_comm.wait_context = true;
// 	}
// }

// fabric::fabric_msg fabric::send(fabric_ep &fabric_ep, fabric_comm &fabric_comm, const void *buffer, size_t size, uint32_t tag)
// {
// 	int ret;
// 	fabric_msg msg = {};

// 	// tag format 24 bits rank_peer 24 bits rank_self_in_peer 16 bits tag
// 	uint64_t aux_rank_peer = fabric_comm.rank_peer;
// 	uint64_t aux_rank_self_in_peer = fabric_comm.rank_self_in_peer;
// 	uint64_t aux_tag = tag;
// 	uint64_t tag_send = (aux_rank_peer << 40) | (aux_rank_self_in_peer << 16) | aux_tag;

// 	fabric_comm.context.rank = fabric_comm.rank_peer;

// 	debug_info("[FABRIC] [fabric_send] Start size " << size << " rank_peer " << fabric_comm.rank_peer << " rank_self_in_peer " << fabric_comm.rank_self_in_peer << " tag " << tag << " send_context " << (void *)&fabric_comm.context);

// 	if (size > fabric_ep.info->tx_attr->inject_size)
// 	{
// 		fid_ep *p_tx_ep = fabric_ep.use_scalable_ep ? fabric_ep.tx_ep : fabric_ep.ep;
// 		do
// 		{
// 			ret = fi_tsend(p_tx_ep, buffer, size, NULL, fabric_comm.fi_addr, tag_send, &fabric_comm.context);

// 			if (ret == -FI_EAGAIN)
// 				(void)fi_cq_read(fabric_ep.cq, NULL, 0);
// 		} while (ret == -FI_EAGAIN);

// 		if (ret)
// 		{
// 			printf("error posting send buffer (%d)\n", ret);
// 			msg.error = -1;
// 			return msg;
// 		}

// 		debug_info("[FABRIC] [fabric_send] Waiting on rank_peer " << fabric_comm.rank_peer);

// 		wait(fabric_ep, fabric_comm);
// 	}
// 	else
// 	{
// 		fid_ep *p_tx_ep = fabric_ep.use_scalable_ep ? fabric_ep.tx_ep : fabric_ep.ep;
// 		do
// 		{
// 			ret = fi_tinject(p_tx_ep, buffer, size, fabric_comm.fi_addr, tag_send);

// 			if (ret == -FI_EAGAIN)
// 				(void)fi_cq_read(fabric_ep.cq, NULL, 0);
// 		} while (ret == -FI_EAGAIN);
// 		debug_info("[FABRIC] [fabric_send] fi_tinject for rank_peer " << fabric_comm.rank_peer);
// 	}

// 	msg.size = size;

// 	msg.tag = tag_send & 0x0000'0000'0000'FFFF;
// 	msg.rank_peer = (tag_send & 0xFFFF'FF00'0000'0000) >> 40;
// 	msg.rank_self_in_peer = (tag_send & 0x0000'00FF'FFFF'0000) >> 16;

// 	debug_info("[FABRIC] [fabric_send] msg size " << msg.size << " rank_peer " << msg.rank_peer << " rank_self_in_peer " << msg.rank_self_in_peer << " tag " << msg.tag << " error " << msg.error);
// 	debug_info("[FABRIC] [fabric_send] End = " << size);
// 	return msg;
// }

// fabric::fabric_msg fabric::recv(fabric_ep &fabric_ep, fabric_comm &fabric_comm, void *buffer, size_t size, uint32_t tag)
// {
// 	int ret;
// 	fabric_msg msg = {};

// 	uint64_t mask = 0;
// 	// tag format 24 bits rank_self_in_peer 24 bits rank_peer 16 bits tag
// 	uint64_t aux_rank_peer = fabric_comm.rank_peer;
// 	uint64_t aux_rank_self_in_peer = fabric_comm.rank_self_in_peer;
// 	uint64_t aux_tag = tag;
// 	uint64_t tag_recv = (aux_rank_self_in_peer << 40) | (aux_rank_peer << 16) | aux_tag;

// 	fabric_comm.context.rank = fabric_comm.rank_peer;

// 	if (fabric_comm.rank_peer == FABRIC_ANY_RANK)
// 	{
// 		// mask = 0x0000'00FF'FFFF'0000;
// 		// mask = 0xFFFF'FF00'0000'0000;
// 		mask = 0xFFFF'FFFF'FFFF'0000;
// 	}

// 	debug_info("[FABRIC] [fabric_recv] Start size " << size << " rank_peer " << fabric_comm.rank_peer << " rank_self_in_peer " << fabric_comm.rank_self_in_peer << " tag " << tag << " recv_context " << (void *)&fabric_comm.context);

// 	fid_ep *p_rx_ep = fabric_ep.use_scalable_ep ? fabric_ep.rx_ep : fabric_ep.ep;
// 	do
// 	{
// 		ret = fi_trecv(p_rx_ep, buffer, size, NULL, fabric_comm.fi_addr, tag_recv, mask, &fabric_comm.context);

// 		if (ret == -FI_EAGAIN)
// 			(void)fi_cq_read(fabric_ep.cq, NULL, 0);
// 	} while (ret == -FI_EAGAIN);

// 	if (ret)
// 	{
// 		printf("error posting recv buffer (%d)\n", ret);
// 		msg.error = -1;
// 		return msg;
// 	}

// 	debug_info("[FABRIC] [fabric_recv] Waiting on rank_peer " << fabric_comm.rank_peer);

// 	wait(fabric_ep, fabric_comm);

// 	msg.size = size;
// 	// msg.error = fabric_comm.context.entry.err;

// 	msg.tag = fabric_comm.context.entry.tag & 0x0000'0000'0000'FFFF;
// 	msg.rank_self_in_peer = (fabric_comm.context.entry.tag & 0xFFFF'FF00'0000'0000) >> 40;
// 	msg.rank_peer = (fabric_comm.context.entry.tag & 0x0000'00FF'FFFF'0000) >> 16;

// 	debug_info("[FABRIC] [fabric_recv] msg size " << msg.size << " rank_peer " << msg.rank_peer << " rank_self_in_peer " << msg.rank_self_in_peer << " tag " << msg.tag << " error " << msg.error);
// 	debug_info("[FABRIC] [fabric_recv] End = " << size);
// 	return msg;
// }

// int fabric::close(fabric_ep &fabric_ep, fabric_comm &fabric_comm)
// {
// 	int ret = 0;
// 	debug_info("[FABRIC] [fabric_close_comm] Start");

// 	std::unique_lock<std::mutex> lock(s_mutex);

// 	remove_addr(fabric_ep, fabric_comm);

// 	fabric_ep.m_comms.erase(fabric_comm.rank_peer);

// 	debug_info("[FABRIC] [fabric_close_comm] End = " << ret);

// 	return ret;
// }

// int fabric::destroy(fabric_ep &fabric_ep)
// {
// 	int ret = 0;

// 	debug_info("[FABRIC] [fabric_destroy] Start");

// 	std::unique_lock<std::mutex> lock(s_mutex);

// 	destroy_thread_cq(fabric_ep);

// 	if (fabric_ep.tx_ep)
// 	{
// 		debug_info("[FABRIC] [fabric_close_comm] Close tx_context");
// 		ret = fi_close(&fabric_ep.tx_ep->fid);
// 		if (ret)
// 			printf("warning: error closing tx_context (%d)\n", ret);
// 		fabric_ep.tx_ep = nullptr;
// 	}

// 	if (fabric_ep.rx_ep)
// 	{
// 		debug_info("[FABRIC] [fabric_close_comm] Close rx_context");
// 		ret = fi_close(&fabric_ep.rx_ep->fid);
// 		if (ret)
// 			printf("warning: error closing rx_context (%d)\n", ret);
// 		fabric_ep.rx_ep = nullptr;
// 	}

// 	if (fabric_ep.ep)
// 	{
// 		debug_info("[FABRIC] [fabric_close_comm] Close endpoint");
// 		ret = fi_close(&fabric_ep.ep->fid);
// 		if (ret)
// 			printf("warning: error closing EP (%d)\n", ret);
// 		fabric_ep.ep = nullptr;
// 	}

// 	if (fabric_ep.av)
// 	{
// 		debug_info("[FABRIC] [fabric_close_comm] Close address vector");
// 		ret = fi_close(&fabric_ep.av->fid);
// 		if (ret)
// 			printf("warning: error closing AV (%d)\n", ret);
// 		fabric_ep.av = nullptr;
// 	}

// 	if (fabric_ep.cq)
// 	{
// 		debug_info("[FABRIC] [fabric_close_comm] Close completion queue");
// 		ret = fi_close(&fabric_ep.cq->fid);
// 		if (ret)
// 			printf("warning: error closing CQ (%d)\n", ret);
// 		fabric_ep.cq = nullptr;
// 	}

// 	if (fabric_ep.domain)
// 	{
// 		debug_info("[FABRIC] [fabric_destroy] Close domain");
// 		ret = fi_close(&fabric_ep.domain->fid);
// 		if (ret)
// 			printf("warning: error closing domain (%d)\n", ret);
// 		fabric_ep.domain = nullptr;
// 	}

// 	if (fabric_ep.fabric)
// 	{
// 		debug_info("[FABRIC] [fabric_destroy] Close fabric");
// 		ret = fi_close(&fabric_ep.fabric->fid);
// 		if (ret)
// 			printf("warning: error closing fabric (%d)\n", ret);
// 		fabric_ep.fabric = nullptr;
// 	}

// 	if (fabric_ep.hints)
// 	{
// 		debug_info("[FABRIC] [fabric_destroy] Free hints ");
// 		fi_freeinfo(fabric_ep.hints);
// 		fabric_ep.hints = nullptr;
// 	}

// 	if (fabric_ep.info)
// 	{
// 		debug_info("[FABRIC] [fabric_destroy] Free info ");
// 		fi_freeinfo(fabric_ep.info);
// 		fabric_ep.info = nullptr;
// 	}

// 	debug_info("[FABRIC] [fabric_destroy] End = " << ret);

// 	return ret;
// }

// } // namespace LFI
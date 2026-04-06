#-
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2022 En-Wei Wu
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

# wtap_setup N: create wtap devices 0..N-1 with full mutual visibility.
wtap_setup()
{
	local n="$1" i j

	for i in $(seq 0 $((n - 1))); do
		wtapctl device create "${i}"
	done
	wtapctl vis open
	for i in $(seq 0 $((n - 1))); do
		for j in $(seq 0 $((n - 1))); do
			[ "$i" -ne "$j" ] && wtapctl vis add "${i}" "${j}"
		done
	done
}

# wtap_teardown N: destroy wtap devices 0..N-1.
wtap_teardown()
{
	local n="$1" i

	for i in $(seq 0 $((n - 1))); do
		wtapctl device delete "${i}" || true
	done
}

# ----------------------------------------------------------------------------

atf_test_case "mesh_ping_test" "cleanup"
mesh_ping_test_head()
{
	atf_set descr 'Mesh mode: two wtap nodes exchange ICMP via mesh VAPs'
	atf_set require.user root
	atf_set require.progs jail wtapctl
	atf_set require.kmods wtap
}

mesh_ping_test_body()
{
	local mesh_a_nm mesh_b_nm jname success

	success=0
	jname="mesh_ping_test"

	wtap_setup 2

	mesh_a_nm=$(ifconfig wlan create wlandev wtap0 wlanmode mesh meshid wtap_test)
	mesh_b_nm=$(ifconfig wlan create wlandev wtap1 wlanmode mesh meshid wtap_test)

	echo "${mesh_a_nm}" > mesh_a_nm.txt
	echo "${mesh_b_nm}" > mesh_b_nm.txt

	jail -c name="${jname}" persist vnet \
	    vnet.interface="${mesh_a_nm}" allow.raw_sockets

	jexec "${jname}" ifconfig "${mesh_a_nm}" inet 10.0.0.1/24 up
	ifconfig "${mesh_b_nm}" inet 10.0.0.2/24 up

	# Mesh mode needs time to elect a root and build the path table.
	sleep 10

	# Poll until both sides have a BSSID, then ping.
	for i in $(seq 1 5); do
		mesh_a_bssid=$(jexec "${jname}" ifconfig "${mesh_a_nm}" | \
		    awk '/bssid/{print $9}')
		mesh_b_bssid=$(ifconfig "${mesh_b_nm}" | awk '/bssid/{print $9}')

		if [ -n "${mesh_a_bssid}" ] && [ -n "${mesh_b_bssid}" ]; then
			if ping -S 10.0.0.2 -c 2 10.0.0.1; then
				success=1
			fi
			break
		fi
		sleep 1
	done

	atf_check_equal 1 "${success}"
}

mesh_ping_test_cleanup()
{
	local mesh_a_nm mesh_b_nm jname

	jname="mesh_ping_test"

	mesh_a_nm=$(cat mesh_a_nm.txt 2>/dev/null || true)
	mesh_b_nm=$(cat mesh_b_nm.txt 2>/dev/null || true)
	rm -f mesh_a_nm.txt mesh_b_nm.txt

	if [ -n "${mesh_a_nm}" ]; then
		jexec "${jname}" ifconfig "${mesh_a_nm}" destroy 2>/dev/null || true
	fi
	[ -n "${mesh_b_nm}" ] && ifconfig "${mesh_b_nm}" destroy 2>/dev/null || true

	jail -r "${jname}" 2>/dev/null || true

	wtap_teardown 2
}

# ----------------------------------------------------------------------------

atf_test_case "adhoc_ping_test" "cleanup"
adhoc_ping_test_head()
{
	atf_set descr 'Ad-hoc mode: two wtap nodes IBSS-merge and exchange ICMP'
	atf_set require.user root
	atf_set require.progs jail wtapctl
	atf_set require.kmods wtap
}

adhoc_ping_test_body()
{
	local adhoc_a_nm adhoc_b_nm jname success

	success=0
	jname="adhoc_ping_test"

	wtap_setup 2

	adhoc_a_nm=$(ifconfig wlan create wlandev wtap0 wlanmode adhoc ssid wtap_test)
	adhoc_b_nm=$(ifconfig wlan create wlandev wtap1 wlanmode adhoc ssid wtap_test)

	echo "${adhoc_a_nm}" > adhoc_a_nm.txt
	echo "${adhoc_b_nm}" > adhoc_b_nm.txt

	jail -c name="${jname}" persist vnet \
	    vnet.interface="${adhoc_a_nm}" allow.raw_sockets

	jexec "${jname}" ifconfig "${adhoc_a_nm}" inet 10.0.0.1/24 up
	ifconfig "${adhoc_b_nm}" inet 10.0.0.2/24 up

	# Wait for IBSS merge (both nodes share the same BSSID).
	for i in $(seq 1 10); do
		adhoc_a_bssid=$(jexec "${jname}" ifconfig "${adhoc_a_nm}" | \
		    awk '/bssid/{print $9}')
		adhoc_b_bssid=$(ifconfig "${adhoc_b_nm}" | awk '/bssid/{print $9}')

		if [ -n "${adhoc_a_bssid}" ] && [ -n "${adhoc_b_bssid}" ] \
		    && [ "${adhoc_a_bssid}" = "${adhoc_b_bssid}" ]; then
			if ping -S 10.0.0.2 -c 2 10.0.0.1; then
				success=1
			fi
			break
		fi
		sleep 1
	done

	atf_check_equal 1 "${success}"
}

adhoc_ping_test_cleanup()
{
	local adhoc_a_nm adhoc_b_nm jname

	jname="adhoc_ping_test"

	adhoc_a_nm=$(cat adhoc_a_nm.txt 2>/dev/null || true)
	adhoc_b_nm=$(cat adhoc_b_nm.txt 2>/dev/null || true)
	rm -f adhoc_a_nm.txt adhoc_b_nm.txt

	if [ -n "${adhoc_a_nm}" ]; then
		jexec "${jname}" ifconfig "${adhoc_a_nm}" destroy 2>/dev/null || true
	fi
	[ -n "${adhoc_b_nm}" ] && ifconfig "${adhoc_b_nm}" destroy 2>/dev/null || true

	jail -r "${jname}" 2>/dev/null || true

	wtap_teardown 2
}

# ----------------------------------------------------------------------------

atf_test_case "sta_hostap_ping_test" "cleanup"
sta_hostap_ping_test_head()
{
	atf_set descr 'STA/HostAP: STA associates with AP and exchanges ICMP'
	atf_set require.user root
	atf_set require.progs jail wtapctl
	atf_set require.kmods wtap
}

sta_hostap_ping_test_body()
{
	local hostap_nm sta_nm hostap_bssid sta_bssid jname success

	success=0
	jname="sta_hostap_ping_test"

	wtap_setup 2

	hostap_nm=$(ifconfig wlan create wlandev wtap0 wlanmode hostap ssid wtap_test)
	sta_nm=$(ifconfig wlan create wlandev wtap1 wlanmode sta ssid wtap_test)

	echo "${hostap_nm}" > hostap_nm.txt
	echo "${sta_nm}" > sta_nm.txt

	jail -c name="${jname}" persist vnet \
	    vnet.interface="${hostap_nm}" allow.raw_sockets

	jexec "${jname}" ifconfig "${hostap_nm}" inet 10.0.0.1/24 up
	ifconfig "${sta_nm}" inet 10.0.0.2/24 up

	# Wait for the AP to be ready.
	for i in $(seq 1 5); do
		hostap_bssid=$(jexec "${jname}" ifconfig "${hostap_nm}" | \
		    awk '/bssid/{print $9}')
		[ -n "${hostap_bssid}" ] && break
		sleep 1
	done

	if [ -z "${hostap_bssid}" ]; then
		atf_fail "AP did not become ready"
	fi

	"$(atf_get_srcdir)/sta_assoc" "${sta_nm}" "${hostap_bssid}"

	# Wait for the STA to associate.
	for i in $(seq 1 5); do
		sta_bssid=$(ifconfig "${sta_nm}" | awk '/bssid/{print $9}')

		if [ -n "${sta_bssid}" ] && \
		    [ "${sta_bssid}" = "${hostap_bssid}" ]; then
			if ping -S 10.0.0.2 -c 2 10.0.0.1; then
				success=1
			fi
			break
		fi
		sleep 1
	done

	atf_check_equal 1 "${success}"
}

sta_hostap_ping_test_cleanup()
{
	local hostap_nm sta_nm jname

	jname="sta_hostap_ping_test"

	hostap_nm=$(cat hostap_nm.txt 2>/dev/null || true)
	sta_nm=$(cat sta_nm.txt 2>/dev/null || true)
	rm -f hostap_nm.txt sta_nm.txt

	if [ -n "${hostap_nm}" ]; then
		jexec "${jname}" ifconfig "${hostap_nm}" destroy 2>/dev/null || true
	fi
	[ -n "${sta_nm}" ] && ifconfig "${sta_nm}" destroy 2>/dev/null || true

	jail -r "${jname}" 2>/dev/null || true

	wtap_teardown 2
}

# ----------------------------------------------------------------------------

atf_test_case "monitor_tcpdump_check" "cleanup"
monitor_tcpdump_check_head()
{
	atf_set descr 'Monitor mode: tcpdump captures ICMP frames between STA and AP'
	atf_set require.user root
	atf_set require.progs jail tcpdump wtapctl
	atf_set require.kmods wtap
}

monitor_tcpdump_check_body()
{
	local hostap_nm sta_nm monitor_nm
	local hostap_bssid sta_bssid jname assoc success

	assoc=0
	success=0
	jname="monitor_tcpdump_check"

	wtap_setup 3

	hostap_nm=$(ifconfig wlan create wlandev wtap0 wlanmode hostap ssid wtap_test)
	sta_nm=$(ifconfig wlan create wlandev wtap1 wlanmode sta ssid wtap_test)
	monitor_nm=$(ifconfig wlan create wlandev wtap2 wlanmode monitor)

	echo "${hostap_nm}"  > hostap_nm.txt
	echo "${sta_nm}"     > sta_nm.txt
	echo "${monitor_nm}" > monitor_nm.txt

	jail -c name="${jname}" persist vnet \
	    vnet.interface="${hostap_nm}" allow.raw_sockets

	jexec "${jname}" ifconfig "${hostap_nm}" inet 10.0.0.1/24 up
	ifconfig "${sta_nm}" inet 10.0.0.2/24 up
	ifconfig "${monitor_nm}" up

	# Wait for the AP to be ready.
	for i in $(seq 1 5); do
		hostap_bssid=$(jexec "${jname}" ifconfig "${hostap_nm}" | \
		    awk '/bssid/{print $9}')
		[ -n "${hostap_bssid}" ] && break
		sleep 1
	done

	if [ -z "${hostap_bssid}" ]; then
		atf_fail "AP did not become ready"
	fi

	"$(atf_get_srcdir)/sta_assoc" "${sta_nm}" "${hostap_bssid}"

	# Wait for the STA to associate.
	for i in $(seq 1 5); do
		sta_bssid=$(ifconfig "${sta_nm}" | awk '/bssid/{print $9}')

		if [ -n "${sta_bssid}" ] && \
		    [ "${sta_bssid}" = "${hostap_bssid}" ]; then
			assoc=1
			break
		fi
		sleep 1
	done

	if [ "${assoc}" = "1" ]; then
		# Capture ICMP traffic between STA and AP on the monitor VAP.
		tcpdump -y IEEE802_11_RADIO -i "${monitor_nm}" \
		    -w tcpdump.out \
		    "icmp and (src 10.0.0.1 or src 10.0.0.2)" &
		sleep 1
		ping -S 10.0.0.2 -c 5 10.0.0.1
		pkill tcpdump || true

		# Allow tcpdump time to flush its output.
		for i in $(seq 1 5); do
			if [ -s tcpdump.out ]; then
				success=1
				break
			fi
			sleep 1
		done
	fi

	atf_check_equal 1 "${success}"
}

monitor_tcpdump_check_cleanup()
{
	local hostap_nm sta_nm monitor_nm jname

	jname="monitor_tcpdump_check"

	hostap_nm=$(cat hostap_nm.txt 2>/dev/null || true)
	sta_nm=$(cat sta_nm.txt 2>/dev/null || true)
	monitor_nm=$(cat monitor_nm.txt 2>/dev/null || true)
	rm -f hostap_nm.txt sta_nm.txt monitor_nm.txt tcpdump.out

	if [ -n "${hostap_nm}" ]; then
		jexec "${jname}" ifconfig "${hostap_nm}" destroy 2>/dev/null || true
	fi
	[ -n "${sta_nm}" ] && ifconfig "${sta_nm}" destroy 2>/dev/null || true
	[ -n "${monitor_nm}" ] && ifconfig "${monitor_nm}" destroy 2>/dev/null || true

	jail -r "${jname}" 2>/dev/null || true

	wtap_teardown 3
}

# ----------------------------------------------------------------------------

atf_init_test_cases()
{
	atf_add_test_case "mesh_ping_test"
	atf_add_test_case "adhoc_ping_test"
	atf_add_test_case "sta_hostap_ping_test"
	atf_add_test_case "monitor_tcpdump_check"
}

./tpcds-lv ./../lvconfig.conf 0 64 3 >> ./tpcds_lvl0_64.txt

./tpcds-lv ./../lvconfig.conf 1 64 3 >> ./tpcds_lvl1_64.txt

./tpcds-lv ./../lvconfig.conf 2 64 3 >> ./tpcds_lvl2_64.txt



./cab-lv table ./../lvconfig.conf ./../src/CAB/sample/ 64 wh/table.db/s >> ./cab_table_64.txt

./cab-lv read ./../lvconfig.conf ./../src/CAB/sample/ >> ./cab_read.txt

./cab-lv write ./../lvconfig.conf ./../src/CAB/sample/ >> ./cab_write.txt

./cab-lv mixed ./../lvconfig.conf ./../src/CAB/sample/ 8 4 >> ./cab_mixed_8_4.txt


./ycsbc-lv -config ./../lvconfigA.conf -db LakeVillalvl0 -threads 1 -P ./../src/YCSB-C/workloads/workloada.spec >> ./A/1/ycsb_a_lvl0.txt

./ycsbc-lv -config ./../lvconfigB.conf -db LakeVillalvl0 -threads 1 -P ./../src/YCSB-C/workloads/workloadb.spec >> ./B/1/ycsb_b_lvl0.txt

./ycsbc-lv -config ./../lvconfigC.conf -db LakeVillalvl0 -threads 1 -P ./../src/YCSB-C/workloads/workloadc.spec >> ./C/1/ycsb_c_lvl0.txt

./ycsbc-lv -config ./../lvconfigD.conf -db LakeVillalvl0 -threads 1 -P ./../src/YCSB-C/workloads/workloadd.spec >> ./D/1/ycsb_d_lvl0.txt

./ycsbc-lv -config ./../lvconfigF.conf -db LakeVillalvl0 -threads 1 -P ./../src/YCSB-C/workloads/workloadf.spec >> ./F/1/ycsb_f_lvl0.txt


./ycsbc-lv -config ./../lvconfigA.conf -db LakeVillalvl0 -threads 8 -P ./../src/YCSB-C/workloads/workloada.spec >> ./A/8/ycsb_a_lvl0.txt

./ycsbc-lv -config ./../lvconfigB8.conf -db LakeVillalvl0 -threads 8 -P ./../src/YCSB-C/workloads/workloadb.spec >> ./B/8/ycsb_b_lvl0.txt

./ycsbc-lv -config ./../lvconfigC8.conf -db LakeVillalvl0 -threads 8 -P ./../src/YCSB-C/workloads/workloadc.spec >> ./C/8/ycsb_c_lvl0.txt

./ycsbc-lv -config ./../lvconfigD8.conf -db LakeVillalvl0 -threads 8 -P ./../src/YCSB-C/workloads/workloadd.spec >> ./D/8/ycsb_d_lvl0.txt

./ycsbc-lv -config ./../lvconfigF8.conf -db LakeVillalvl0 -threads 8 -P ./../src/YCSB-C/workloads/workloadf.spec >> ./F/8/ycsb_f_lvl0.txt



./ycsbc-lv -config ./../lvconfigA64.conf -db LakeVillalvl0 -threads 64 -P ./../src/YCSB-C/workloads/workloada.spec >> ./A/64/ycsb_a_lvl0.txt

./ycsbc-lv -config ./../lvconfigB64.conf -db LakeVillalvl0 -threads 64 -P ./../src/YCSB-C/workloads/workloadb.spec >> ./B/64/ycsb_b_lvl0.txt

./ycsbc-lv -config ./../lvconfigC64.conf -db LakeVillalvl0 -threads 64 -P ./../src/YCSB-C/workloads/workloadc.spec >> ./C/64/ycsb_c_lvl0.txt

./ycsbc-lv -config ./../lvconfigD64.conf -db LakeVillalvl0 -threads 64 -P ./../src/YCSB-C/workloads/workloadd.spec >> ./D/64/ycsb_d_lvl0.txt

./ycsbc-lv -config ./../lvconfigF64.conf -db LakeVillalvl0 -threads 64 -P ./../src/YCSB-C/workloads/workloadf.spec >> ./F/64/ycsb_f_lvl0.txt
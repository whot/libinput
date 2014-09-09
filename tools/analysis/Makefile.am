results/png:
	$(MKDIR_P) results/png

results/vectors: results/target-analysis.dat results/png
	for file in results/vectors/*.xml*; do \
		file=`basename $$file`; \
		gnuplot -e "file='results/vectors/$$file'; outfile='results/png/$${file/.xml/.png}'" vectors.gnuplot; \
	done

results/vectors-all: results/target-analysis.dat results/png
	for file in `ls results/vectors/*.xml* | sed -e "s/.xml.*/.xml/" | uniq `; do \
		file=`basename $$file`; \
		file=$${file/.xml*/.xml}; \
		gnuplot -e "file='results/vectors/$$file'; outfile='results/png/$${file/.xml/.png}'" vectors-all.gnuplot; \
	done
	touch $@

hist_graphs =  \
	 /hist-target-counts-grouped-by-ID.png \
	 /hist-time-to-click-mean-grouped-by-ID.png \
	 /hist-path-mean-grouped-by-ID.png \
	 /hist-path-absolute-mean-grouped-by-ID.png \
	 /hist-overshoot-mean-grouped-by-ID.png \
	 /hist-overshoot-for-path-mean-grouped-by-ID.png \
	 /hist-overshoot-for-distance-mean-grouped-by-ID.png \
	 /hist-questionnaire.png \
	 /hist-questionnaire-likert-counts.png \
	 /hist-questionnaire-preferable.png

graphs = \
	 /ID-time-mapping.png \
	 /ID-path-mapping.png \
	 /ID-path-mapping-absolute.png \
	 /ID-mapping.png \
	 $(hist_graphs)

shadow_dat_files = \
		   results/hist-target-counts-grouped-by-ID.dat \
		   results/hist-time-to-click-mean-grouped-by-ID.dat \
		   results/hist-path-mean-grouped-by-ID.dat \
		   results/hist-path-absolute-mean-grouped-by-ID.dat \
		   results/hist-overshoot-mean-grouped-by-ID.dat \
		   results/hist-overshoot-for-path-mean-grouped-by-ID.dat \
		   results/hist-overshoot-for-distance-mean-grouped-by-ID.dat \
		   results/hist-questionnaire.dat \
		   results/hist-questionnaire-likert-counts.dat \
		   results/hist-questionnaire-preferable.dat

results/png/hist-%.png: $(shadow_dat_files)
	gnuplot -e "file='results/hist-$*.dat'; outfile='results/png/hist-$*.png'" hist-$*.gnuplot

results/png/ID-%.png: results/target-analysis.dat
	gnuplot -e "file='$^'; outfile='results/png/ID-$*.png'" ID-$*.gnuplot

results/target-analysis.dat: ../userstudy-analysis.py
	$(MKDIR_P) results/vectors
	$(AM_V_GEN)$(PYTHON) ../userstudy-analysis.py -o results $(USERSTUDYRESULTSDIR)

$(shadow_dat_files): results/target-analysis.dat

mappings: results/png $(graphs:/%=results/png/%)

all-local: mappings 

vectors: results/vectors results/vectors-all 

clean-local:
	rm -rf results

import java.io.IOException;
import java.util.StringTokenizer;

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.io.IntWritable;
import org.apache.hadoop.io.Text;
import org.apache.hadoop.mapreduce.Job;
import org.apache.hadoop.mapreduce.Mapper;
import org.apache.hadoop.mapreduce.Reducer;
import org.apache.hadoop.mapreduce.lib.input.FileInputFormat;
import org.apache.hadoop.mapreduce.lib.output.FileOutputFormat;

public class SleepTime {

    public static class TokenizerMapper extends Mapper<Object, Text, IntWritable, IntWritable> {

        private final static IntWritable one = new IntWritable(1);
        private IntWritable hour = new IntWritable();

        public void map(Object key, Text value, Context context) throws IOException, InterruptedException {

            //Parse Tweet
            String tweet = value.toString();
            if (tweet.length() < 1) return;
            StringTokenizer lines = new StringTokenizer(tweet, "\n");
            if (!lines.hasMoreTokens()) return;
            String time = lines.nextToken();
            if (time.charAt(0) != 'T' || !lines.hasMoreTokens()) return;
            lines.nextToken(); // Username
            if (!lines.hasMoreTokens()) return;
            String post = lines.nextToken();
            if (post.charAt(0) != 'W') return;

            //Check for "sleep" and add hour
            if (post.indexOf("sleep") != -1){
                time = time.substring(time.indexOf('-')+7, time.indexOf('-')+9);
                hour.set(Integer.parseInt(time));
                context.write(hour, one);
            }

        }
    }

    public static class IntSumReducer extends Reducer<IntWritable, IntWritable, IntWritable, IntWritable> {
        private IntWritable result = new IntWritable();

        public void reduce(IntWritable key, Iterable<IntWritable> values, Context context) throws IOException, InterruptedException {
            
            int sum = 0;
            for (IntWritable val : values) {
                sum += val.get();
            }
            result.set(sum);
            context.write(key, result);

        }
    }

    public static void main(String[] args) throws Exception {
        Configuration conf = new Configuration();
        conf.set("textinputformat.record.delimiter", "\n\n");
        Job job = Job.getInstance(conf, "Time of Day");
        job.setJarByClass(SleepTime.class);
        job.setMapperClass(TokenizerMapper.class);
        job.setCombinerClass(IntSumReducer.class);
        job.setReducerClass(IntSumReducer.class);
        job.setOutputKeyClass(IntWritable.class);
        job.setOutputValueClass(IntWritable.class);
        FileInputFormat.addInputPath(job, new Path(args[0]));
        FileOutputFormat.setOutputPath(job, new Path(args[1]));
        System.exit(job.waitForCompletion(true) ? 0 : 1);
    }
}

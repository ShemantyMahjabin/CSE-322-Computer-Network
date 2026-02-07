package offline;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.net.Socket;
import java.io.InputStreamReader;

public class Client {
   private String serverip="127.0.0.1";
    private int serverport=12345;
    private Socket socket;
    private BufferedReader reader;
    private PrintWriter writer;
     private static final String COMPLETE = "<END_OF_RESPONSE>";
    private static final String FILE_DOWNLODED = "FILE_DOWNLODED";
    private String myname;
    public void startClient(String host ,int port){
        try{
            socket=new Socket(serverip,serverport);
            reader=new BufferedReader(new InputStreamReader(socket.getInputStream()));
            writer=new PrintWriter(socket.getOutputStream(),true);
            BufferedReader consoleReader=new BufferedReader(new InputStreamReader(System.in));
            String serverMessage=reader.readLine();
            System.out.println(serverMessage);
            String userInput=consoleReader.readLine();
            myname = userInput;
            writer.println(userInput);
            
            serverMessage=reader.readLine();
            if(serverMessage.equalsIgnoreCase("Username already taken. Connection closing.")){
                System.out.println(serverMessage);
                //socket.close();
                return;

            }
            System.out.println(serverMessage);
            
            String request;
            while(true){
                
                System.out.println("Enter a command");
                System.out.println("Press 1 to see the userList on this server");
                System.out.println("Write 2 to see own uploaded files");
                System.out.println("Write 3 to lookup for any publicly uploaded files of users");
                System.out.println("Press 4 to download");
                System.out.println("Press 5 to upload");
                System.out.println("Press 6 to request for any file");
                System.out.println("Press 7 to see own inbox messages");
                System.out.println("Press 8 to go offline");
                System.out.println("Press 9 to view upload/download history");
                request=consoleReader.readLine();

                if(request.equalsIgnoreCase("1")){
                    writer.println(request);
                    writer.flush();
                    String responseLine;
                    responseLine = reader.readLine();
                    while(!(responseLine.equals(COMPLETE))){
                        System.out.println(responseLine);
                        responseLine = reader.readLine();
                    }
                }
                else if(request.equalsIgnoreCase("2")){
                    writer.println(request);
                    writer.flush();
                    String responseLine;
                    responseLine = reader.readLine();
                    while(!(responseLine.equals(COMPLETE))){
                        System.out.println(responseLine);
                        responseLine = reader.readLine();
                    }
                }
                else if(request.equalsIgnoreCase("3")){
                    writer.println(request);
                    writer.flush();
                    String responseLine;
                    responseLine = reader.readLine();
                    while(!(responseLine.equals(COMPLETE))){
                        System.out.println(responseLine);
                        responseLine = reader.readLine();
                    }
                }
                else if(request.equalsIgnoreCase("4")){
                    BufferedReader br=new BufferedReader(new InputStreamReader(System.in));
                    System.out.println("Whoose file you wanna download?");
                    String owner=br.readLine();
                    System.out.println("Which file you wanna download?");
                    String filename=br.readLine();
                    String path = "server_files/" + owner + "/" + filename;
                    String savepath= "client_files/" + myname + "/"+ filename;
                    writer.println(request);
                    writer.println(path);
                    writer.println(owner);
                    writer.println(filename);
                    String serverResponse=reader.readLine();
                    if(serverResponse.equalsIgnoreCase("FILE_FOUND")){
                        System.out.println("Downloading file...");
                        try(BufferedOutputStream bos=new BufferedOutputStream(new FileOutputStream(savepath))){
                            String line;
                            while((line = reader.readLine())!=null && !line.equals(FILE_DOWNLODED)){
                                byte[] data=line.getBytes();
                                bos.write(data);
                                bos.write('\n');
                            }
                            bos.flush();
                        }
                        System.out.println("File downloaded successfully and saved to "+savepath);
                    }else{
                        System.out.println("File not found on server.");
                    }

                }
                else if(request.equalsIgnoreCase("5")){
                    BufferedReader br=new BufferedReader(new InputStreamReader(System.in));
                    System.out.println("Enter file path to upload:");
                    String filepath=br.readLine();
                    File file=new File(filepath);
                    if(!file.exists()){
                        System.out.println("File does not exist. Please check the path.");
                        return;
                    }
                    String filetype;
                    String reqid;
                    System.out.println("Is this a upload based on any request? (Y/N): ");
                    String response=br.readLine();
                    if(response.equalsIgnoreCase("Y")){
                        System.out.println("Enter request ID:");
                        reqid=br.readLine();
                        filetype="public";
                    }else{
                        System.out.println("Is this a public or private upload? (public/private): ");
                        filetype=br.readLine();
                        reqid="0";
                    }
                    String filename=file.getName();
                    writer.println("5");
                    writer.println(filename);
                    writer.println(filetype);
                    writer.println(response);
                    writer.println(reqid);

                    long fileLength=file.length();
                    writer.println(fileLength);
                    String serverreply=reader.readLine();
                    if(!serverreply.equalsIgnoreCase("Yes")){
                        System.out.println(reader.readLine());
                        reader.readLine();
                        return;
                    }
                    int allocatedChunkSize=Integer.parseInt(reader.readLine());
                    String fileid=reader.readLine();
                    int totalChunks=(int)Math.ceil((double)fileLength/allocatedChunkSize);
                    if(totalChunks==0) totalChunks++;
                    writer.println(totalChunks);
                    try (BufferedInputStream bis = new BufferedInputStream(new FileInputStream(file))) {
                        byte[] buffer = new byte[allocatedChunkSize];
                        int bytesread;
                        java.io.OutputStream out = socket.getOutputStream();
                        while ((bytesread = bis.read(buffer)) != -1) {
                            out.write(buffer, 0, bytesread);
                            out.flush();

                            String ack = reader.readLine();
                            if (ack == null || !ack.equalsIgnoreCase("OK")) {
                                System.out.println("Error in file upload. Connection terminated. Ack=" + ack);
                                return;
                            }
                            System.out.println("Uploading Chunks...");
                        }
                    }
                   
                    writer.flush();
                    String finalResponse=reader.readLine();
                    System.out.println(finalResponse);
                    while(!finalResponse.equalsIgnoreCase(COMPLETE)){
                        finalResponse=reader.readLine();
                        System.out.println(finalResponse);
                    }
                }
                else if(request.equalsIgnoreCase("6")){
                    BufferedReader br=new BufferedReader(new InputStreamReader(System.in));
                    System.out.println("Please provide file description");
                    String description=br.readLine();
                    //System.out.println("Enter requestid:");
                    //String reqid=br.readLine();
                    System.out.println("Enter recipient:");
                    String recipient=br.readLine();
                    writer.println(request);
                    writer.println(description);
                    //writer.println(reqid);
                    writer.println(recipient);
                    String serverResponse=reader.readLine();
                    System.out.println(serverResponse);
                }
                else if(request.equalsIgnoreCase("7")){
                    writer.println(request);
                    writer.flush();
                    String responseLine;
                    responseLine = reader.readLine();
                    while(!(responseLine.equals(COMPLETE))){
                        System.out.println(responseLine);
                        responseLine = reader.readLine();
                    }
                }
                else if(request.equalsIgnoreCase("8")){
                    writer.println(request);
                    writer.flush();
                    System.out.println("Going offline. Connection closed.");
                    String responseLine;
                    responseLine = reader.readLine();
                    while(!(responseLine.equals(COMPLETE))){
                        System.out.println(responseLine);
                        responseLine = reader.readLine();
                    }
                    break;
                }
                else if(request.equalsIgnoreCase("9")){
                    writer.println(request);
                    writer.flush();
                    String responseLine = reader.readLine();
                    while(responseLine != null && !(responseLine.equals(COMPLETE))){
                        System.out.println(responseLine);
                        responseLine = reader.readLine();
                    }
                }
                else
                {
                    System.out.println("Invalid command....!");
                }
            } 
        }catch (Exception e) {
            e.printStackTrace();
        }
    }
    public static void main(String[] args) {
        String host = "localhost";
        int port = 12345;
        Client client=new Client();
        client.startClient(host,port);
    }
}
 